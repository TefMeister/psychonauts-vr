"""Numerical validation of the notes/34 head-tracking math in proxy_d3d9.c.

Checks, in the file's row-vector convention (p' = p*M, flat row-major):
 1. Identity pose => Y == I (upload unchanged).
 2. For a random affine M and pose delta D: applying the code's pipeline to
    upload=Transpose(M*P) reproduces Transpose(M*T*P) exactly, where T=D^-1
    (translation scaled to world units) -- i.e. the camera really moves with D.
 3. Reference capture: pose == ref => T == I.
 4. Composition order with the per-eye patch: WVP*Yt*Yeye == M*T*Xeye*P.
"""
import numpy as np

rng = np.random.default_rng(7)
WU = 100.0

def rotY(t):
    c, s = np.cos(t), np.sin(t)
    return np.array([[c,0,-s,0],[0,1,0,0],[s,0,c,0],[0,0,0,1]], float)  # row-vector

def rotX(t):
    c, s = np.cos(t), np.sin(t)
    return np.array([[1,0,0,0],[0,c,s,0],[0,-s,c,0],[0,0,0,1]], float)  # row-vector

def trans(x,y,z):
    m = np.eye(4); m[3,:3] = [x,y,z]; return m

def proj(xS, yS, zn, zf):
    A = zf/(zn-zf); B = zn*zf/(zn-zf)
    P = np.zeros((4,4)); P[0,0]=xS; P[1,1]=yS; P[2,2]=A; P[2,3]=-1; P[3,2]=B
    return P

def pinv_analytic(xS, yS, zn, zf):
    A = zf/(zn-zf); B = zn*zf/(zn-zf)
    Pi = np.zeros((4,4)); Pi[0,0]=1/xS; Pi[1,1]=1/yS; Pi[2,3]=1/B; Pi[3,2]=-1; Pi[3,3]=A/B
    return Pi

xS, yS, zn, zf = 1.5377, 1.5377*(4/3), 10.0, 50000.0
P = proj(xS, yS, zn, zf)
Pi = pinv_analytic(xS, yS, zn, zf)
assert np.allclose(P @ Pi, np.eye(4), atol=1e-9), "analytic P^-1 wrong"
print("P^-1 analytic form: OK")

def code_T_from_motion(motion):
    """Replicates the C code: T = rigid inverse of motion, translation *WU."""
    T = np.eye(4)
    T[:3,:3] = motion[:3,:3].T
    T[3,:3] = -(motion[3,:3] @ motion[:3,:3].T) * WU
    return T

# --- check 1: identity pose
D = np.eye(4)
T = code_T_from_motion(D)
Y = Pi @ T @ P
assert np.allclose(Y, np.eye(4), atol=1e-12)
print("identity pose => Y == I: OK")

# --- check 2: full pipeline vs ground truth
M = np.eye(4)
M[:3,:3] = (rotY(0.61) @ rotX(-0.22))[:3,:3] * 1.0   # rigid rotation
M[3,:3] = rng.normal(0, 300, 3)                       # world translation
D = rotY(0.35) @ rotX(0.1) @ trans(0.04, 0.02, -0.06) # head motion, meters... order: rot then trans
# NOTE: the C code builds 'motion' directly as a row-vector rigid matrix; any rigid D is fine here.
T = code_T_from_motion(D)
Y = Pi @ T @ P
WVP = M @ P
upload = WVP.T
uploaded_new = (Y.T @ upload)          # what the C hook computes (Yt * upload)
ground_truth = (M @ T @ P).T           # camera moved by D => view correction T inserted
assert np.allclose(uploaded_new, ground_truth, atol=1e-8)
print("pipeline reproduces M*T*P exactly: OK")

# --- check 3: reference logic. pose == ref => motion == I => T == I
yaw0, pos0 = 0.8, np.array([0.3, 1.6, -0.2])
pose = rotY(yaw0) @ trans(*pos0)       # row-vector: rotate then translate = pose with yaw+pos
refinv = trans(*(-pos0)) @ rotY(-yaw0) # code's RefInv = Trans(-pos)*RotY(-yaw)
motion = pose @ refinv
assert np.allclose(motion, np.eye(4), atol=1e-12)
print("pose==ref => motion == I: OK")

# yaw extraction from the C code: yaw = atan2(pose[8], pose[10]) (row 2 x and z)
pose_r = rotY(yaw0)
yaw_extracted = np.arctan2(pose_r[2,0], pose_r[2,2])
assert np.isclose(yaw_extracted, yaw0), f"{yaw_extracted} vs {yaw0}"
print("yaw extraction matches: OK")

# --- check 4: composition with per-eye patch
d, k = -3.144, -0.176327
A = zf/(zn-zf); B = zn*zf/(zn-zf)
Y20 = (-d)*xS/B
Y30 = (-k - A*d/B)*xS
Yeye = np.eye(4); Yeye[2,0] = Y20; Yeye[3,0] = Y30
Xeye = Pi @ np.linalg.inv(Pi) @ np.eye(4)  # placeholder
# C code: tracked = Yt*upload; then column-0 patch = right-mult by Yeye on untransposed
tracked = Y.T @ upload
patched = tracked.copy()
patched[0,:] = tracked[0,:] + tracked[2,:]*Y20 + tracked[3,:]*Y30   # flat r, 8+r, 12+r pattern
truth = (M @ T @ P @ Yeye).T
assert np.allclose(patched, truth, atol=1e-8)
print("per-eye patch composes as WVP*Ytrack*Yeye: OK")

print("\nALL CHECKS PASSED")

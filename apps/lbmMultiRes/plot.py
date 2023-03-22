import os
import numpy as np
import matplotlib.pyplot as plt

#only use the colums for y and Re=1000

fig, axes = plt.subplots(nrows=1, ncols=1, figsize=(4, 3), dpi=200)


y_neon, u_neon = np.loadtxt('NeonMultiResLBM_5000.dat', unpack=True, usecols=(0, 1))
p = y_neon.argsort()
u_neon = u_neon[p]
y_neon = y_neon[p]
axes.plot(y_neon, 25*u_neon, 'b-', label='Neon MultiRes LBM')


y_ref, u_ref = np.loadtxt('ghia1982.dat', unpack=True, skiprows=2, usecols=(0, 1))
axes.plot(y_ref, u_ref, 'ms', label='Ghia et al. 1982')

axes.legend()
axes.set_xlabel(r'Y')
axes.set_ylabel(r'U')
plt.tight_layout()
plt.savefig("MultiResNeon_vs_ghia1982.png")
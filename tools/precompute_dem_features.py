"""Freeze a simple, independent normal-variation feature texture from a float32 DEM.

The resulting R32F texture is an ablation input, NOT a conservative geometric
or radiometric error bound. It must not replace the established reference.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
DEFAULT_DEM=ROOT/'assets/dem/mount_st_helens_usgs_10m_1921_f32.raw'
DEFAULT_OUT=ROOT/'assets/dem/mount_st_helens_features_v1_f32.raw'


def features_from_height(h: np.ndarray, spacing: float, angle_degrees: float=25.0) -> np.ndarray:
    """Max angle to a four-neighbor normal, normalized to the chosen angle."""
    if h.ndim!=2 or min(h.shape)<3 or not np.all(np.isfinite(h)) or spacing<=0 or angle_degrees<=0:
        raise ValueError('Expected a finite 2D height grid and positive spacing/angle')
    dz,dx=np.gradient(h.astype(np.float32,copy=False),np.float32(spacing))
    nx=-dx
    ny=np.ones_like(h,dtype=np.float32)
    nz=-dz
    inverse=np.reciprocal(np.sqrt(nx*nx+ny*ny+nz*nz))
    nx*=inverse;ny*=inverse;nz*=inverse
    minimum_dot=np.ones_like(h,dtype=np.float32)
    for xshift,zshift in ((1,0),(-1,0),(0,1),(0,-1)):
        # Interior uses the exact closest samples; boundaries clamp to the edge.
        nxs=np.roll(nx,(zshift,xshift),axis=(0,1))
        nys=np.roll(ny,(zshift,xshift),axis=(0,1))
        nzs=np.roll(nz,(zshift,xshift),axis=(0,1))
        if zshift>0:
            nxs[:zshift]=nx[:zshift];nys[:zshift]=ny[:zshift];nzs[:zshift]=nz[:zshift]
        if zshift<0:
            nxs[zshift:]=nx[zshift:];nys[zshift:]=ny[zshift:];nzs[zshift:]=nz[zshift:]
        if xshift>0:
            nxs[:,:xshift]=nx[:,:xshift];nys[:,:xshift]=ny[:,:xshift];nzs[:,:xshift]=nz[:,:xshift]
        if xshift<0:
            nxs[:,xshift:]=nx[:,xshift:];nys[:,xshift:]=ny[:,xshift:];nzs[:,xshift:]=nz[:,xshift:]
        dot=nx*nxs+ny*nys+nz*nzs
        np.minimum(minimum_dot,dot,out=minimum_dot)
    angle=np.arccos(np.clip(minimum_dot,-1.0,1.0))
    np.divide(angle,np.deg2rad(angle_degrees),out=angle)
    np.clip(angle,0,1,out=angle)
    return np.asarray(angle,dtype='<f4',order='C')


def sha(path:Path)->str:
    result=hashlib.sha256()
    with path.open('rb') as src:
        for chunk in iter(lambda:src.read(1024*1024),b''):result.update(chunk)
    return result.hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,default=DEFAULT_DEM)
    parser.add_argument('--output',type=Path,default=DEFAULT_OUT)
    parser.add_argument('--size',type=int,default=1921)
    parser.add_argument('--spacing',type=float,default=10.0)
    parser.add_argument('--threshold-degrees',type=float,default=25.0)
    opt=parser.parse_args()
    expected=opt.size*opt.size*4
    if opt.input.stat().st_size!=expected:raise ValueError(f'Expected {expected} bytes')
    h=np.fromfile(opt.input,dtype='<f4').reshape(opt.size,opt.size)
    features=features_from_height(h,opt.spacing,opt.threshold_degrees)
    opt.output.parent.mkdir(parents=True,exist_ok=True)
    features.tofile(opt.output)
    manifest={
      'format':'little-endian float32 / GL_R32F row-major', 'size':opt.size,
      'feature':'max four-neighbor normal angle / threshold (clamped 0..1)',
      'threshold_degrees':opt.threshold_degrees, 'sample_spacing_m':opt.spacing,
      'source_file':opt.input.name,'source_sha256':sha(opt.input),
      'feature_sha256':sha(opt.output),
      'min':float(features.min()),'max':float(features.max()),
      'mean':float(features.mean()), 'nonzero_fraction':float((features>0.001).mean()),
      'usage':'Exploratory proxy only; not a certified error bound'
    }
    opt.output.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))

if __name__=='__main__':main()

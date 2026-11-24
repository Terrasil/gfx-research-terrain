"""Fast standalone numerical and wiring checks for the offline DEM feature ablation."""
import sys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from precompute_dem_features import features_from_height


def test_plane_has_no_feature():
    y,x=np.mgrid[:21,:21].astype('float32')
    f=features_from_height(0.5*x-0.2*y,1.0)
    assert np.max(f) < 0.003


def test_sharp_ridge_has_feature():
    y,x=np.mgrid[:21,:21].astype('float32')
    f=features_from_height(np.abs(x-10)*0.6,1.0)
    assert f.shape==(21,21) and np.max(f[:,9:12])>0.1
    assert np.all(np.isfinite(f)) and np.min(f)>=0 and np.max(f)<=1


def test_runtime_ablation_is_opt_in():
    cfg=(ROOT/'include/gfx/terrain/ExperimentConfig.hpp').read_text()
    frag=(ROOT/'shaders/tess_surface.tesc').read_text()
    cpp=(ROOT/'src/TerrainResearchApp.cpp').read_text()
    assert 'offline_feature_ablation = false' in cfg
    assert '(uUseOfflineFeatures ? precomputedEdgeFeature(a,b) : edgeFeatureStrength(a,b))' in frag
    assert 'precomputedEdgeFeature' in frag
    assert 'scene_ == SurfaceScene::MountStHelensDem' in cpp
    assert 'mount_st_helens_features_v1_f32.raw' in cpp

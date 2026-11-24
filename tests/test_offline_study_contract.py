"""Offline DEM benchmark must remain separate from the dissertation's frozen matrix."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def test_offline_case_flags_and_suite_routing():
    runner=(ROOT/'src/BenchmarkRunner.cpp').read_text()
    gui=(ROOT/'src/TerrainResearchApp.cpp').read_text()
    main=(ROOT/'src/main.cpp').read_text()
    assert 'void BenchmarkRunner::build_offline_feature_cases()' in runner
    assert 'BenchmarkStudy::OfflineFeature' in runner
    assert 'c.offline_feature_ablation = arm==1;' in runner
    assert 'if (scene_changed || feature_changed) rebuild_scene();' in gui
    assert 'offline_feature_map' in runner
    assert '--offline-feature-study' in main

def test_offline_report_logic_on_synthetic_data():
    import tempfile
    from pathlib import Path
    temp=tempfile.TemporaryDirectory()
    tmp_path=Path(temp.name)
    import importlib.util
    spec=importlib.util.spec_from_file_location('offline',ROOT/'tools/analyze_offline_features.py')
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    import csv
    raw=[];quality=[];idx=0
    for d in ['13','18']:
        for budget in ['0.5','1','2']:
            for arm in range(3):
                row={'case':str(idx),'study':'offline-feature-study',
                'method':'tessellation-error-feature' if arm<2 else 'tessellation-error',
                'offline_feature_map':str(int(arm==1)),'base_distance':d,
                'error_budget_px':budget,'gpu_ms':'1.0','triangles':'100'}
                raw.append(row)
                quality.append({k:row[k] for k in ['case','offline_feature_map']} | {
                    'image_foreground_rmse':'0.001','boundary_p95_distance_px':'0.5'})
                idx+=1
    for path, rows in ((tmp_path/'raw.csv',raw),(tmp_path/'quality.csv',quality)):
        with path.open('w',newline='') as f:
            writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    m.run(tmp_path)
    with (tmp_path/'offline_feature_pairs.csv').open() as f:
        assert len(list(csv.DictReader(f)))==6
    temp.cleanup()

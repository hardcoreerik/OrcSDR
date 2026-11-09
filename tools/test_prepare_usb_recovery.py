"""Check USB override generation against a stock ESP-IDF checkout."""
from pathlib import Path
import argparse
import importlib.util
import shutil
import tempfile
from unittest.mock import patch

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--idf', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('prepare_usb', root / 'apps/orcsdr-tab5/tools/prepare_usb_recovery.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory(prefix='orcsdr-usb-regression-') as temporary:
    folder = Path(temporary)
    sdk = folder / 'sdk'
    (sdk / 'tools/cmake').mkdir(parents=True)
    (sdk / 'components').mkdir()
    shutil.copytree(args.idf / 'components/usb', sdk / 'components/usb',
                    ignore=shutil.ignore_patterns('host_test', 'test_apps'))
    version = sdk / 'tools/cmake/version.cmake'
    original_version = (args.idf / 'tools/cmake/version.cmake').read_text()
    version.write_text(original_version)
    for target in ('shutil.copytree', 'subprocess.run', 'Path.write_text'):
        failed_build = folder / target.replace('.', '-')
        failure = OSError('injected preparation failure')
        with patch.object(module.shutil if target.startswith('shutil') else
                          module.subprocess if target.startswith('subprocess') else Path,
                          target.split('.')[-1], side_effect=failure):
            try:
                module.prepare(sdk, failed_build)
            except OSError as error:
                assert error is failure
            else:
                raise AssertionError('Expected injected failure')
        assert not list(failed_build.glob('orcsdr-usb-*')), target
        assert module.prepare(sdk, failed_build).is_dir(), target

    component = module.prepare(sdk, folder / 'build')
    assert module.prepare(sdk, folder / 'build') == component
    assert (sdk / 'components/usb/hub.c').read_bytes() == (args.idf / 'components/usb/hub.c').read_bytes()

    def expect_failure(build, message):
        try:
            module.prepare(sdk, build)
        except RuntimeError as error:
            assert message in str(error), str(error)
        else:
            raise AssertionError(f'Expected rejection containing {message}')

    (component / 'hub.c').write_text('changed')
    expect_failure(folder / 'build', 'Modified USB override')
    version.write_text('set(IDF_VERSION_MAJOR 6)\nset(IDF_VERSION_MINOR 1)\nset(IDF_VERSION_PATCH 0)\n')
    expect_failure(folder / 'wrong-version', 'requires ESP-IDF')
    version.write_text(original_version)
    (sdk / 'components/usb/hub.c').write_text('changed')
    expect_failure(folder / 'wrong-source', 'Unexpected SDK USB source')
print('PASS failure cleanup and retry, preparation, reuse, SDK preservation, modified override, unsupported SDK and changed source')

#!/usr/bin/env python3
"""Package matched camera artifacts; never installs an APK or module."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import zipfile

source, build, output = map(pathlib.Path, sys.argv[1:])
if subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True).strip():
    raise SystemExit('Package only a clean, committed source tree')
output.mkdir(parents=True, exist_ok=True)
module = output / 'magisk'
shutil.copytree(source / 'module', module, dirs_exist_ok=True)
(module / 'bin').mkdir(exist_ok=True)
(module / 'kmod').mkdir(exist_ok=True)
shutil.copy2(build / 'native/linux-camerad', module / 'bin/linux-camerad')
shutil.copy2(build / 'kmod/kernel/linux_camera.ko', module / 'kmod/linux_camera.ko')
shutil.copy2(build / 'android/LinuxCamera-0.1.5.apk', output / 'LinuxCamera-0.1.5.apk')
for name in ['README.md', 'PROTOCOL.md', 'linux-cameractl', 'install-linux.sh']:
    shutil.copy2(source / name, output / name)
(output / 'tests').mkdir(exist_ok=True)
shutil.copy2(build / 'native/camera-lifecycle-test', output / 'tests/camera-lifecycle-test')
shutil.copy2(build / 'native/camera-measure', output / 'tests/camera-measure')
shutil.copytree(source / 'diagnostics', output / 'validation', dirs_exist_ok=True)
shutil.copy2(build / 'tests/packing.log', output / 'validation/packing.log')
shutil.copy2(build / 'tests/packing-equivalence.log', output / 'validation/packing-equivalence.log')
for name in ['packing-equivalence', 'packing-benchmark']:
    shutil.copy2(build / 'native' / name, output / 'tests' / name)
shutil.copy2(build / 'android/verification.txt', output / 'validation/apk-signature.txt')
kernel = pathlib.Path('/bigdata/hdmi-los-build/work/companion-probe-20261005/kernel-output')
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
manifest = {
    'version': '0.1.5',
    'stage': 'fast packing qualified on Xperia: four-lens 720p images and main 1080p30/60, 4K30 stream-copy; application qualification remains separate',
    'source_commit': subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip(),
    'kernel_release': '5.15.176-gd00ba216ccda',
    'kernel_commit': 'd00ba216ccda5d4fcc0d864729ae69d5b63d860c',
    'vendor_commit': 'ec2e039129f2b8f93fdfe62a8c6a595efb63d496',
    'kernel_config_sha256': digest(kernel / '.config'),
    'kernel_symvers_sha256': digest(kernel / 'Module.symvers'),
    'kernel_compiler': 'Android Clang 19.0.1 r536225',
    'ndk': '29.0.14206865',
    'artifacts': {str(p.relative_to(output)): digest(p) for p in sorted(output.rglob('*')) if p.is_file() and p.name not in ['manifest.json', 'SHA256SUMS'] and p.suffix != '.zip'},
}
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
shutil.copy2(output / 'manifest.json', module / 'manifest.json')
with zipfile.ZipFile(output / 'LinuxCameraRouting-0.1.5.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(module.rglob('*')):
        if path.is_file(): archive.write(path, path.relative_to(module))
for name in ['install-linux.sh', 'linux-cameractl', 'tests/camera-lifecycle-test', 'tests/camera-measure', 'tests/packing-equivalence', 'tests/packing-benchmark']:
    (output / name).chmod(0o755)
(output / 'SHA256SUMS').write_text(''.join(digest(p) + '  ' + str(p.relative_to(output)) + '\n' for p in sorted(output.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS'))
print(output)

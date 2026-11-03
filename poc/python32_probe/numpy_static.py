from pathlib import Path
import subprocess, shlex, json, hashlib
root = Path('/py')
build = root/'numpy-build-target'
out = root/'numpy-static'
out.mkdir(exist_ok=True)
modules = ['core._multiarray_umath', 'core._multiarray_tests', 'fft._pocketfft_internal', 'linalg._umath_linalg',
           'random._bounded_integers', 'random._common', 'random.bit_generator',
           'random._generator', 'random._mt19937', 'random._pcg64',
           'random._philox', 'random._sfc64', 'random.mtrand']
objects, libraries = [], []
header = ['/* Generated explicit built-in registrations; no dynamic loader. */']
for module in modules:
    folder, leaf = module.split('.')
    target = Path('numpy')/folder/(leaf+'.cpython-311.so')
    assert (build/target).is_file(), target
    lines = subprocess.check_output(['ninja','-t','commands',str(target)],cwd=build,text=True).splitlines()
    args = shlex.split(lines[-1])
    for arg in args:
        if arg.endswith('.o') or arg.endswith('.a'):
            path = str((build/arg).resolve())
            group = objects if arg.endswith('.o') else libraries
            if path not in group: group.append(path)
    header.append('PyMODINIT_FUNC PyInit_'+leaf+'(void);')
header.append('static int register_numpy(void) {')
for module in modules:
    leaf = module.split('.')[-1]
    header.append('  if (PyImport_AppendInittab("numpy.'+module+'", PyInit_'+leaf+') < 0) return -1;')
header.extend(['  return 0;', '}'])
objects = list({hashlib.sha256(Path(p).read_bytes()).hexdigest(): p for p in reversed(objects)}.values())
(out/'numpy_inittab.h').write_text('\n'.join(header)+'\n')
(out/'objects.rsp').write_text('-Wl,--start-group\n'+'\n'.join(objects+libraries)+'\n-Wl,--end-group\n')
(out/'manifest.json').write_text(json.dumps(dict(modules=modules,objects=objects,libraries=libraries),indent=2))
print('objects',len(objects),'libraries',len(libraries),'modules',len(modules))

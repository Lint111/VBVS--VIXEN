#!/usr/bin/env python3
"""Run through with-test-lock.sh: exercise cache locking and check invalidation."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

repository = Path(__file__).resolve().parents[1]
scratch = repository / "build" / "cache-contract-tests"
scratch.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(dir=scratch))
module = repository / "VIXEN" / "cmake"


def run(*command, success=True):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if (result.returncode == 0) != success:
        print(result.stdout, flush=True)
        raise AssertionError(f"unexpected status {result.returncode}: {command}")
    return result.stdout


source = root / "check-source"
source.mkdir()
schemas = source / "schemas"
schemas.mkdir()
(schemas / "a.cs").write_text("a")
generated = source / "output.g.h"
generated.write_text("a")
count = root / "checks.count"
checker = source / "checker.py"
checker.write_text("""from pathlib import Path
import sys
schema, output, count = map(Path, sys.argv[1:])
count.write_text(str(int(count.read_text()) + 1 if count.exists() else 1))
expected = ''.join(p.read_text() for p in sorted(schema.glob('*.cs')))
raise SystemExit(0 if output.read_text() == expected else 1)
""")
(source / "CMakeLists.txt").write_text(f"""cmake_minimum_required(VERSION 3.20)
project(CacheCheckContract LANGUAGES NONE)
include("{module}/VixenChecks.cmake")
file(GLOB inputs CONFIGURE_DEPENDS "{schemas}/*.cs")
vixen_add_checked_target(contract_check ALL
    COMMAND "{sys.executable}" "{checker}" "{schemas}" "{generated}" "{count}"
    DEPENDS ${{inputs}} "{generated}" "{checker}"
    COMMENT "checking cache contract")
""")
binary = root / "check-build"
run("cmake", "-S", str(source), "-B", str(binary), "-G", "Ninja")
build = ("cmake", "--build", str(binary), "--parallel", "4")
run(*build)
stamp = binary / "checks" / "contract_check.stamp"
initial_stamp = stamp.stat().st_mtime_ns
run(*build)
assert count.read_text() == "1" and stamp.stat().st_mtime_ns == initial_stamp
generated.write_text("drift")
run(*build, success=False)
assert count.read_text() == "2" and stamp.stat().st_mtime_ns == initial_stamp
generated.write_text("a")
run(*build)
assert count.read_text() == "3"
added = schemas / "b.cs"
added.write_text("")
os.utime(added, (1, 1))
run(*build)
assert count.read_text() == "4"
added.unlink()
run(*build)
assert count.read_text() == "5"
print("PASS: no-op stamp stable; generated drift fails; old schema addition/removal invalidates", flush=True)

cache = root / "tool-cache"
builder = root / "builder.py"
builder.write_text("""from pathlib import Path
import sys
cache = Path(sys.argv[1]); count = cache / 'builds.count'
count.write_text(str(int(count.read_text()) + 1 if count.exists() else 1))
(cache / 'bin').mkdir(exist_ok=True)
for name in ['CodegenTool.dll', 'CodegenTool.deps.json', 'CodegenTool.runtimeconfig.json', 'Dependency.dll']:
    (cache / 'bin' / name).write_text('cached bytes')
""")
clients = []
for index in range(2):
    local = root / f"client-{index}"
    local.mkdir()
    config = local / "config.cmake"
    setup = local / "setup.cmake"
    setup.write_text(f"""set(VIXEN_KERNEL_CACHE_DIR "{root}/snapshots")
include("{module}/VixenKernelSnapshot.cmake")
vixen_codegen_build_config("{config}" "{cache}" "{cache}/bin" "{local}/bin" "{local}/ready"
    "{sys.executable}" "{builder}" "{cache}")
""")
    run("cmake", "-P", str(setup))
    clients.append(subprocess.Popen(["cmake", f"-DVIXEN_CODEGEN_BUILD_CONFIG={config}",
                                    "-P", str(module / "VixenBuildCodegen.cmake")],
                                   text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT))
for client in clients:
    output, _ = client.communicate()
    assert client.returncode == 0, output
assert (cache / "builds.count").read_text() == "1"
for index in range(2):
    local = root / f"client-{index}"
    assert (local / "bin" / "Dependency.dll").read_text() == "cached bytes"
    (local / "bin" / "CodegenTool.dll").unlink()
    run("cmake", f"-DVIXEN_CODEGEN_BUILD_CONFIG={local}/config.cmake", "-P", str(module / "VixenBuildCodegen.cmake"))
    assert (local / "bin" / "CodegenTool.dll").read_text() == "cached bytes"
assert (cache / "builds.count").read_text() == "1"
print("PASS: two concurrent clients build once; deleted local binaries restore from cache", flush=True)
print(f"Evidence: {root}", flush=True)

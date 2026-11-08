# Self-contained check for ctest-manifest.ps1: synthetic inventory + two JUnit slices (a resumed run).
$ErrorActionPreference = 'Stop'
$dir = Join-Path ([IO.Path]::GetTempPath()) ("ctest-manifest-test-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path "$dir\build" | Out-Null
try {
  foreach ($n in 'a','b','c') { Set-Content "$dir\build\$n.exe" 'x' }
  $tests = @()
  foreach ($i in 1..3) { $tests += @{ name = "A.t$i"; command = @("$dir\build\a.exe") } }
  foreach ($i in 1..2) { $tests += @{ name = "B.t$i"; command = @("$dir\build\b.exe") } }
  @{ tests = $tests } | ConvertTo-Json -Depth 5 | Set-Content "$dir\inv.json"
  # slice 1 stopped early: A.t1 pass, A.t2 pass, A.t3 and B.* notrun
  Set-Content "$dir\s1.xml" '<testsuite><testcase name="A.t1" status="run"/><testcase name="A.t2" status="run"/><testcase name="A.t3" status="notrun"><skipped/></testcase><testcase name="B.t1" status="notrun"><skipped/></testcase><testcase name="B.t2" status="notrun"><skipped/></testcase></testsuite>'
  Start-Sleep -Milliseconds 50
  # resumed slice: A.t3 pass, B.t1 pass, B.t2 fail
  Set-Content "$dir\s2.xml" '<testsuite><testcase name="A.t3" status="run"/><testcase name="B.t1" status="run"/><testcase name="B.t2" status="fail"><failure/></testcase></testsuite>'
  & (Join-Path $PSScriptRoot 'ctest-manifest.ps1') -InventoryJsonPath "$dir\inv.json" -JunitPath @("$dir\s1.xml", "$dir\s2.xml") -BuildDir "$dir\build" -ManifestPath "$dir\m.tsv"
  $rows = @{}
  Get-Content "$dir\m.tsv" | Select-Object -Skip 1 | ForEach-Object { $f = $_ -split "`t"; $rows[(Split-Path $f[0] -Leaf)] = $f }
  $expect = @{ 'a.exe' = '3','3','0','0','PASS'; 'b.exe' = '2','1','1','0','FAIL'; 'c.exe' = '0','0','0','0','NO_REGISTERED_CTEST_SMOKE' }
  foreach ($k in $expect.Keys) {
    $got = @($rows[$k][1], $rows[$k][2], $rows[$k][3], $rows[$k][4], $rows[$k][5])
    if (($got -join '|') -ne ($expect[$k] -join '|')) { throw "FAIL $k : got $($got -join '|') expected $($expect[$k] -join '|')" }
  }
  # slice 1 alone must read INCOMPLETE for a.exe, not PASS
  & (Join-Path $PSScriptRoot 'ctest-manifest.ps1') -InventoryJsonPath "$dir\inv.json" -JunitPath @("$dir\s1.xml") -BuildDir "$dir\build" -ManifestPath "$dir\m1.tsv"
  if ((Get-Content "$dir\m1.tsv" | Where-Object { $_ -like '*a.exe*' }) -notlike '*INCOMPLETE') { throw 'FAIL: unfinished slice not INCOMPLETE' }
  Write-Output 'ctest-manifest test: PASS'
} finally { Remove-Item -Recurse -Force $dir }

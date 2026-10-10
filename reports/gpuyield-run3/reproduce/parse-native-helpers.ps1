$ErrorActionPreference='Stop'
foreach($file in @('C:\vixen-native\gpuyield\.tmp\run-native-producer.ps1','C:\vixen-native\gpuyield\.tmp\native-recovery-matrix.ps1')) {
 $tokens=$null; $errors=$null
 $ast=[System.Management.Automation.Language.Parser]::ParseFile($file,[ref]$tokens,[ref]$errors)
 if($errors.Count -ne 0){$errors | Format-List; exit 1}
 Write-Output "PowerShell syntax PASS: $file"
}

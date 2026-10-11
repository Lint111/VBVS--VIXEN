from pathlib import Path
import subprocess
cases = [
    ('test_rendergraph_tracking', 'SubmitRecord.*', ['TilesConsumeWaitsOnceAndCompleteOnlyAfterFinalSubmit', 'RetirementFailureStopsBeforeTheNextChunkAndFinalSignal', 'SemaphoreOnlyCompletionStillSubmitsItsFence']),
    ('test_rendergraph_dispatch', 'MultiDispatchIntegration.ActualNode*', ['ActualNodeChargesMandatoryUnknownWork', 'ActualNodeUnknownCostCannotBypassStrictAdmission', 'ActualNodeMissingOrUncalibratedProfileIsNotFree']),
    ('test_rendergraph_criticalnodes_pushconstant', 'RayGenerationTileContract.*', ['CompiledShaderPreservesCameraPrefixAndReflectsTileFields']),
]
for executable, selector, names in cases:
    binary = str(Path('build/gpuyield/libraries/RenderGraph/tests') / executable)
    listing = subprocess.run([binary, '--gtest_filter=' + selector, '--gtest_list_tests'], check=True, text=True, capture_output=True).stdout
    for name in names:
        if name not in listing: raise SystemExit(f'Missing required test {name} in {binary}; rebuild before this witness')
    subprocess.run([binary, '--gtest_filter=' + selector], check=True)
print('All seven required contract tests were discovered and executed.')

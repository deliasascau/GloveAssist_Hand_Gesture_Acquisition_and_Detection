$ErrorActionPreference = 'Stop'

$required = @(
  'README.md',
  'docs/01_requirements/system_requirements.md',
  'docs/01_requirements/features_and_scope.md',
  'docs/01_requirements/checkpoints_plan.md',
  'docs/02_architecture/system_architecture.md',
  'docs/03_safety_iec/iec_safety_plan.md',
  'docs/04_misra_compliance/misra_plan.md',
  'docs/05_test_strategy/test_strategy.md',
  'docs/07_traceability/requirements_traceability_matrix.md',
  'docs/diagrams/plantuml/system_context.puml',
  'docs/diagrams/plantuml/dual_mcu_components.puml',
  'docs/diagrams/plantuml/gesture_sequence.puml'
)

$missing = @()
foreach ($path in $required) {
  if (-not (Test-Path $path)) {
    $missing += $path
  }
}

if ($missing.Count -gt 0) {
  Write-Host 'Missing required files:'
  $missing | ForEach-Object { Write-Host " - $_" }
  exit 1
}

Write-Host 'All required documents are present.'
exit 0

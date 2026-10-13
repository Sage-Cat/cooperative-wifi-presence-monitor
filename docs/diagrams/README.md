# PlantUML design catalogue

All views describe a planned means. They are not implemented behavior or
experimental evidence. SVG files are portable renderings; `.puml` files are the
editable source. Diagram labels are semantic; interpretation belongs to the
linked design documents.

| View | Editable source | Rendered view | Interpretation |
|---|---|---|---|
| Components | [components.puml](components.puml) | [SVG](components.svg) | [Architecture](../architecture.md) |
| Data objects | [core_contract.puml](core_contract.puml) | [SVG](core_contract.svg) | [Data model](../data-model.md) |
| Current result states | [result_state.puml](result_state.puml) | [SVG](result_state.svg) | [Lifecycle](../lifecycle.md) |
| Observation processing | [observation_flow.puml](observation_flow.puml) | [SVG](observation_flow.svg) | [Acceptance order](../lifecycle.md#acceptance-order) |
| Gaps, expiry and recovery | [failure_recovery.puml](failure_recovery.puml) | [SVG](failure_recovery.svg) | [Recovery](../lifecycle.md#interrupted-input-and-recovery) |
| Model reload | [model_reload.puml](model_reload.puml) | [SVG](model_reload.svg) | [Model lifecycle](../architecture.md#model-and-configuration-lifecycle) |
| Host deployment | [deployment.puml](deployment.puml) | [SVG](deployment.svg) | [Platforms](../platforms.md) |
| Core user goals | [use_cases.puml](use_cases.puml) | [SVG](use_cases.svg) | [Actors](../use-cases.md) |
| Separate assistance application | [assistance_use_cases.puml](assistance_use_cases.puml) | [SVG](assistance_use_cases.svg) | [Application boundary](../use-cases.md) |

## Render locally

From the repository root, with local PlantUML, Java and Graphviz installed:

```sh
plantuml -checkonly docs/diagrams/*.puml
plantuml -nometadata -tsvg docs/diagrams/*.puml
```

Use `-nometadata` so SVGs do not embed encoded source or working paths. No remote
rendering service or source upload is required. Inspect each changed SVG for
readable labels, complete arrows and clipping before committing it. Do not put
runtime records, deployment identifiers or private configuration in diagrams.

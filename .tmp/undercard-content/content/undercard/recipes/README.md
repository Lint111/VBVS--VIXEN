# Undercard recipe graph (block-out)

This is the content hand-off for the Undercard bootstrap lane. It is intentionally kept
under `VIXEN/.tmp/undercard-content` while the assigned Undercard worktree contains only
its bootstrap `.git` entry. The bootstrap lane should move these files to
`content/undercard/recipes/` and generate the pack schema once its own `render_recipe`
declaration lands.

The intended stable ids are:

| id | graph | authored material / parameter contract |
|---|---|---|
| `undercard:card-template` | `BoxRounded`/`Panel` plate, frame trim, pip lanes, optional seal | `card_type`: 0 industry plain, 1 order brass; `recipeParams[0..5]` are the live six values below |
| `undercard:card-wheat-field` | `InvokeRecipe(card-template)`, static `GlyphOutline` name/text, `InvokeRecipe(icon-wheat)` | plain trim; Wheat icon; `WHEAT FIELD` |
| `undercard:card-hand-mill` | `InvokeRecipe(card-template)`, static `GlyphOutline` name/text, `InvokeRecipe(icon-flour)` | plain trim; Flour icon; `HAND MILL` |
| `undercard:card-order-flour-3` | `InvokeRecipe(card-template)`, static `GlyphOutline` name/text, `InvokeRecipe(icon-flour)` | brass trim; Flour icon; `FLOUR 3` |
| `undercard:icon-wheat` | small block-out SDF glyph | warm wheat material |
| `undercard:icon-flour` | small block-out SDF glyph | pale flour material |
| `undercard:table-slab` | wide bevelled wooden slab | walnut/brown wood, conservative bevel |
| `undercard:mat-felt` | thin inset rectangle plus near-edge hand-zone strip | dark green felt; strip is a distinct green tone |

## Geometry defaults

- Card face: 63 × 88 recipe units.
- Exaggerated card thickness: 1.5 mm, as specified by the design input.
- Frame is a narrow raised trim. Plain industry cards use a dark copper/iron trim;
  order cards use authored brass. The trim choice is a recipe parameter, not a runtime
  card-type branch.
- The table is intentionally a single slab and a single felt inset for this slice.
- Existing look-dev lighting preset: `midday`; no new preset is authored here.

## Live recipe parameters

| index | meaning | values / visual |
|---:|---|---|
| 0 | in-slot fill | 0..1, repeated pips fill from the left |
| 1 | out-slot fill | 0..1, repeated pips fill from the left |
| 2 | stalled | 0/1, amber emission pulse |
| 3 | order count | 0..3 pips |
| 4 | order met | 0/1, seal emission |
| 5 | highlight | 0 none, 1 hover, 2 held |

Pips are `ReadParam` → `Repeat`; the template has no dynamic text. `GlyphOutline`
rows are static per instance, and icon rows are nested with `InvokeRecipe`. The graph
must remain at four hops or fewer.

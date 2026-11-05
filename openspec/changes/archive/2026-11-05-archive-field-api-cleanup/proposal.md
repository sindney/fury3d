## Why

Every optional archive field carried its default twice: once in the member declaration and once as a third argument to `Optional(name, value, default)`. The two copies could drift, and the call-site copy was invisible from the type that owned the field. Separately, an array of struct elements had to be written out by hand at every call site, because the archive could only transfer plain values and had no way to reach a nested type's own `Serialize(Archive&)`.

## What Changes

- **BREAKING (internal C++ API)**: remove the `Optional(name, value, default)` overload. An optional field wrapper takes only a name and a value; an absent optional field leaves the value the type declares.
- Move the remaining constructor-held defaults into their member declarations (`Material::m_Opaque`, `Material::m_AlphaCutoff`, `Shader::m_Type`, `Texture::m_BorderColor`) so every default lives next to the field it belongs to.
- The archive dispatches an array element to the element type's `Serialize(Archive&)` when it has one, and transfers it as a plain value otherwise. `std::vector<bool>` and `std::vector<std::shared_ptr<T>>` get their own element paths.
- Struct-array call sites collapse to a single field transfer. `Serialize(Archive&)` is added to `BuoyancyComponent::FloatPoint`, `GradientKey`, `CurveKey`, `KeyFrame`, `AnimationChannel`, and `InstancedMeshRender::Instance`; element types write their fields and the archive frames them.
- Arrays of shared pointers get their own element path, and the element types (`Shader`, `AnimationChannel`) become default constructible so the archive can create them on load. `Material`'s shader array and `AnimationClip`'s channel array become single field transfers.
- The on-disk scene document is unchanged: the same keys, the same values, and the same array shapes are written and read.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `archive-serialization`: optional field defaults come from the type declaration rather than the call site, and array elements are dispatched through the element type's own `Serialize(Archive&)`.

## Impact

- `engine/Fury/Archive.h`: field wrappers, `TransferNamed`, the array transfer overloads, and a new element dispatch.
- Every `Serialize(Archive&)` implementation under `engine/Fury/`: the optional field calls lose their default argument, and the struct-array call sites use the single-line form.
- Type declarations that now own a default: `Material.h`, `Shader.h`, `Texture.h`.
- Scene documents, `.json`/`.bin` files, and the scene format version are unaffected.

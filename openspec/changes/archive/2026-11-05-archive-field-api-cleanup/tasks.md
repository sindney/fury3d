## 1. Field defaults

- [x] 1.1 Audit every `Optional(name, value, default)` call site against the default declared by the field's type; record the mismatches.
- [x] 1.2 Remove the `Optional(name, value, default)` overload, the `OptionalField` default member, and the default argument of `TransferNamed`.
- [x] 1.3 Drop the third argument at all 47 call sites.
- [x] 1.4 Move the constructor-held defaults into their member declarations (`Material::m_Opaque`, `Material::m_AlphaCutoff`, `Shader::m_Type`, `Texture::m_BorderColor`).

## 2. Array element dispatch

- [x] 2.1 Add the `Serialize(Archive&)` detection trait to `Archive.h`.
- [x] 2.2 Dispatch array elements through the element type's `Serialize(Archive&)` when it has one, and as a plain value otherwise.
- [x] 2.3 Add the `std::vector<bool>` element path.
- [x] 2.4 Add the `std::vector<std::shared_ptr<T>>` element path, with the pointee created on load.
- [x] 2.4a Rebuild a loaded array from the document: clear it before it is entered.
- [x] 2.5 Add `Serialize(Archive&)` to `BuoyancyComponent::FloatPoint`, `GradientKey`, `CurveKey`, `KeyFrame`, `AnimationChannel`, and `InstancedMeshRender::Instance`.
- [x] 2.6 Collapse the hand-written struct-array loops in `BuoyancyComponent`, `AnimationClip`, `ParticleModules`, `InstancedMeshRender`, and `Material` to a single field transfer.
- [x] 2.7 Make the shared_ptr element types default constructible (`Shader`, `AnimationChannel`).

## 3. Verification

- [x] 3.1 Confirm the converted scene document is byte-identical to the pre-change document.
- [x] 3.2 Run the headless test lane.

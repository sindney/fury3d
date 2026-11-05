## Context

The archive API offers four named-field wrappers: `Required`, `Optional`, `Conditional`, and the resource-reference helpers. `Optional` accepted an optional third argument carrying a default value, so a field like `m_TicksPerSecond` stated its default twice: as `int m_TicksPerSecond = 24;` in the header and as `Optional("ticksPerSecond", m_TicksPerSecond, 24)` at the call site. The third argument was only ever read on the load path, when the field was absent from the document, and it was ignored entirely when writing.

Every load path builds the target object first and serializes into it: `Scene::SerializeSceneAssets` calls `T::Create("temp")`, `SceneNode::Serialize` builds components through the registry, and `Scene::Clear` recreates the render settings. The declared default of a freshly constructed object is therefore the value an absent field has to fall back to, which is what the header already states.

Arrays of plain values already had a one-line form, because `Archive::Transfer` carries a `std::vector<T>` overload. Arrays of struct elements did not: the archive had no way to reach a nested type's `Serialize(Archive&)`, so `BuoyancyComponent`, `AnimationClip`, `ParticleModules`, and `InstancedMeshRender` each expanded the loop by hand. `Burst` already had a `Serialize(Archive&)` that nothing but the hand-written loop could reach.

## Goals / Non-Goals

**Goals:**

- One place per default: the type declaration.
- One way to serialize an array, whether the elements are plain values or types with their own `Serialize(Archive&)`.
- The on-disk scene document stays byte-identical.

**Non-Goals:**

- Renaming on-disk keys or normalizing the snake_case/camelCase mix across the format.
- Deriving a field's key from its member name.
- Changing what a required field means, or which fields are required.

## Decisions

**The field wrapper carries no default.** `Optional(name, value)` is the only optional form. A field's default lives in its declaration, so the header is the single source of truth and the archive call site cannot restate it.

*Alternative considered:* keep the third argument and treat it as the authoritative legacy default. Rejected: it keeps two copies of the same fact, and every audited call site already matched the declaration, so it carried no information the declaration did not.

**Defaults move into the declaration.** Four fields still defaulted in a constructor: `Material::m_Opaque`, `Material::m_AlphaCutoff`, `Shader::m_Type`, and `Texture::m_BorderColor`. They move to member declarations so that reading a type tells you its defaults. `Texture::m_BorderColor` changes from `(0, 0, 0, 0)` to `Color::Black`; the alpha of a sampler border color is unused, and this makes the runtime default match the legacy document default.

**Array elements dispatch on the element type.** A `void_t` trait detects `Serialize(Archive&)` on the element type. An element with one is framed as an object by the archive and writes its own fields; an element without one is transferred as a plain value. The framing stays with the archive so that element types keep writing fields only, which is the convention the existing `Burst` and `Gradient` code already follows.

*Alternative considered:* let the element type frame itself. Rejected: it would make the same `Serialize` produce different JSON depending on the caller, and it would force a change to every element type that already exists.

**`std::vector<bool>` gets its own overload.** Its elements are proxies and cannot bind to `bool&`, so the generic path would not compile for it. The overload copies each element into a local, transfers it, and writes it back.

**A loaded array is rebuilt from the document.** The array is cleared before it is entered, so an absent field leaves it empty rather than holding whatever the target object had. The hand-written loops this replaces all cleared first, and the element types they load are default constructed, so an element field the document omits falls back to the element's declared default.

**`std::vector<std::shared_ptr<T>>` gets its own overload.** The element is created on load, so the pointee must be default constructible. The overload is more specialized than the generic `std::vector<T>` one and wins overload resolution, so the generic path is unchanged. `AnimationChannel` and `Shader` gained default arguments on their constructors to satisfy this. A shader loaded from a document takes its name and type from that document, so the placeholder values a default-constructed shader starts with are overwritten.

*Alternative considered:* a factory parameter on the field wrapper, so a pointee with a required constructor argument could still be created. Rejected: it adds a wrapper type, a `TransferNamed` parameter, and a lambda at every call site, for one caller that a default argument on the constructor already covers.

*Alternative considered:* a private constructor on the pointee, exposed to the archive through a creator trait. Rejected: `std::make_shared` needs public access to a constructor, so the type's own factory would have to give up the single-allocation `make_shared` path to keep the constructor private.

## Risks / Trade-offs

[A type that is loaded into an object that already holds a different value keeps that value when the field is absent] -> Every load path constructs the target first, so the fallback is the declared default. A future caller that loads into a reused object has to reset it.

[`Optional("type", type)` in `Shader::Serialize` falls back to the shader's own type instead of `OTHER`] -> The writer always emits the key, so only a hand-edited document is affected, and keeping the shader's type is the more faithful reading of a document that simply omits it.

[`std::vector<std::shared_ptr<T>>` requires a default-constructible pointee] -> The error is a compile error in the template, not a silent misbehavior. `Shader` and `AnimationChannel` are default constructible, so both shared_ptr element arrays use the one-line form.

[Struct element types now need a `Serialize(Archive&)` member] -> The trait fails to compile with a clear message when an element type has no matching overload, which is the same failure the hand-written loops avoided by construction.

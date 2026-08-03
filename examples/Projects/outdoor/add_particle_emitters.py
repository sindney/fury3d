#!/usr/bin/env python3
"""Inject particle emitters into outdoor_water.json in the new asset schema.

ParticleSystem is a top-level asset (like Material / AnimationClip), stored
in `particleSystems[]` and referenced by name from a `ParticleRenderer`
component on a scene node. The injector:

  1. Adds two textures (fire.png, smoke.png) — paths resolve from the
     scene's working dir (Projects/outdoor/).
  2. Adds two Materials (Material_Fire, Material_Smoke).
  3. Adds two ParticleSystems (FireEmber, SmokePlume) to the top-level
     `particleSystems[]` array.
  4. Adds two empty scene nodes ("FireEmber", "SmokePlume") as children
     of "Fire", each carrying one ParticleRenderer component that
     references its ParticleSystem by name + binds a Material.

Idempotent: re-runs replace prior entries by name rather than appending.
"""

from __future__ import annotations

import json
import sys
import uuid
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]


def make_uuid() -> str:
    return str(uuid.uuid4())


def fire_texture_entry() -> dict:
    return {
        "name": "fire.png",
        "uuid": make_uuid(),
        "path": "fire.png",
        "srgb": True,
        "borderColor": [0.0, 0.0, 0.0, 0.0],
        "mipmap": True,
        "filter": "linear",
        "wrap": "clamp_to_edge",
    }


def smoke_texture_entry() -> dict:
    return {
        "name": "smoke.png",
        "uuid": make_uuid(),
        "path": "smoke.png",
        "srgb": False,  # smoke is non-color data
        "borderColor": [0.0, 0.0, 0.0, 0.0],
        "mipmap": True,
        "filter": "linear",
        "wrap": "clamp_to_edge",
    }


def fire_material_entry(tex: dict) -> dict:
    return {
        "name": "Material_Fire",
        "uuid": make_uuid(),
        "opaque": False,
        "alpha_mode": "blend",
        "alpha_cutoff": 0.0,
        "texture_flags": 2,
        "shaders": [],
        "textures": [
            {
                "key": "diffuse_texture",
                "name": tex["name"],
                "uuid": tex["uuid"],
                "path": tex["path"],
                "srgb": tex["srgb"],
                "borderColor": tex["borderColor"],
                "mipmap": tex["mipmap"],
                "filter": tex["filter"],
                "wrap": tex["wrap"],
            }
        ],
        "uniforms": [
            {"key": "diffuse_color", "type": "Uniform4f", "data": [1.0, 1.0, 1.0, 1.0]},
            {"key": "diffuse_factor", "type": "Uniform1f", "data": [1.0]},
            {"key": "emissive_color", "type": "Uniform3f", "data": [0.0, 0.0, 0.0]},
            {"key": "emissive_factor", "type": "Uniform1f", "data": [1.0]},
            {"key": "ambient_color", "type": "Uniform3f", "data": [0.0, 0.0, 0.0]},
            {"key": "ambient_factor", "type": "Uniform1f", "data": [1.0]},
            {"key": "specular_color", "type": "Uniform3f", "data": [0.0, 0.0, 0.0]},
            {"key": "specular_factor", "type": "Uniform1f", "data": [0.0]},
            {"key": "shininess", "type": "Uniform1f", "data": [1.0]},
            {"key": "transparency", "type": "Uniform1f", "data": [0.0]},
            {"key": "material_id", "type": "Uniform1ui", "data": [99]},
        ],
    }


def smoke_material_entry(tex: dict) -> dict:
    return {
        "name": "Material_Smoke",
        "uuid": make_uuid(),
        "opaque": False,
        "alpha_mode": "blend",
        "alpha_cutoff": 0.0,
        "texture_flags": 2,
        "shaders": [],
        "textures": [
            {
                "key": "diffuse_texture",
                "name": tex["name"],
                "uuid": tex["uuid"],
                "path": tex["path"],
                "srgb": tex["srgb"],
                "borderColor": tex["borderColor"],
                "mipmap": tex["mipmap"],
                "filter": tex["filter"],
                "wrap": tex["wrap"],
            }
        ],
        "uniforms": [
            {"key": "diffuse_color", "type": "Uniform4f", "data": [0.55, 0.55, 0.55, 1.0]},
            {"key": "diffuse_factor", "type": "Uniform1f", "data": [0.7]},
            {"key": "emissive_color", "type": "Uniform3f", "data": [0.0, 0.0, 0.0]},
            {"key": "emissive_factor", "type": "Uniform1f", "data": [0.0]},
            {"key": "ambient_color", "type": "Uniform3f", "data": [0.0, 0.0, 0.0]},
            {"key": "ambient_factor", "type": "Uniform1f", "data": [1.0]},
            {"key": "specular_color", "type": "Uniform3f", "data": [0.0, 0.0, 0.0]},
            {"key": "specular_factor", "type": "Uniform1f", "data": [0.0]},
            {"key": "shininess", "type": "Uniform1f", "data": [1.0]},
            {"key": "transparency", "type": "Uniform1f", "data": [0.0]},
            {"key": "material_id", "type": "Uniform1ui", "data": [100]},
        ],
    }


def fire_particle_system_entry() -> dict:
    return {
        "name": "FireEmber",
        "uuid": make_uuid(),
        "type": "ParticleSystem",
        "maxParticles": 512,
        "lifetime": 0.8,
        "startSize": 0.45,
        "emission": {
            "rateOverTime": 30.0,
            "bursts": [
                {"time": 0.0, "count": 15, "probability": 1.0},
            ],
        },
        "shape": {
            "type": 2,  # CONE
            "scale": [0.3, 1.0, 0.3, 1.0],
            "radius": 0.22,
            "angle": 14.0,
        },
        "velocity": {
            "linear": [0.0, 1.0, 0.0, 0.0],
            "speed": 1.0,
            "inheritFromParent": False,
        },
        "colorOverLifetime": {
            "color": {
                "keys": [
                    {"time": 0.0, "color": [1.0, 1.0, 0.6, 1.0]},
                    {"time": 0.5, "color": [1.0, 0.5, 0.1, 0.9]},
                    {"time": 1.0, "color": [0.4, 0.1, 0.05, 0.0]},
                ],
            },
        },
        "sizeOverLifetime": {
            "size": {
                "keys": [
                    {"time": 0.0, "value": 0.8},
                    {"time": 1.0, "value": 0.3},
                ],
            },
        },
        "rotationOverLifetime": {
            "angularVelocity": 0.0,
        },
        "renderer": {
            "materialName": "Material_Fire",
            "blendMode": 1,  # ADDITIVE
        },
    }


def smoke_particle_system_entry() -> dict:
    return {
        "name": "SmokePlume",
        "uuid": make_uuid(),
        "type": "ParticleSystem",
        "maxParticles": 256,
        "lifetime": 3.5,
        "startSize": 0.7,
        "emission": {
            "rateOverTime": 6.0,
            "bursts": [],
        },
        "shape": {
            "type": 1,  # SPHERE
            "scale": [0.3, 0.3, 0.3, 1.0],
            "radius": 0.3,
            "angle": 25.0,
        },
        "velocity": {
            "linear": [0.0, 1.0, 0.0, 0.0],
            "speed": 0.7,
            "inheritFromParent": False,
        },
        "colorOverLifetime": {
            "color": {
                "keys": [
                    {"time": 0.0, "color": [0.55, 0.55, 0.55, 0.0]},
                    {"time": 0.2, "color": [0.55, 0.55, 0.55, 0.7]},
                    {"time": 1.0, "color": [0.55, 0.55, 0.55, 0.0]},
                ],
            },
        },
        "sizeOverLifetime": {
            "size": {
                "keys": [
                    {"time": 0.0, "value": 0.6},
                    {"time": 1.0, "value": 2.0},
                ],
            },
        },
        "rotationOverLifetime": {
            "angularVelocity": 15.0,
        },
        "renderer": {
            "materialName": "Material_Smoke",
            "blendMode": 0,  # ALPHA
        },
    }


def quat_rotate(q: list, v: tuple) -> tuple:
    """Rotate vector v by unit quaternion q=(x,y,z,w)."""
    x, y, z, w = q
    ux, uy, uz = x, y, z
    vx, vy, vz = v
    tx = 2 * (uy * vz - uz * vy)
    ty = 2 * (uz * vx - ux * vz)
    tz = 2 * (ux * vy - uy * vx)
    return (
        vx + w * tx + (uy * tz - uz * ty),
        vy + w * ty + (uz * tx - ux * tz),
        vz + w * tz + (ux * ty - uy * tx),
    )


def emitter_node(system_name: str, counter_rot: list, base: list, h: float) -> dict:
    # counter_rot undoes the Fire node's own rotation so the emitter's
    # local +Y (shape axis + velocity direction) is world-up — particles
    # simulate in owner-local space, so a rotated parent would otherwise
    # tilt the whole plume sideways. The full world delta (base + h·up)
    # is rotated into Fire space (world = fire_pos + R×pos).
    delta = (base[0], base[1] + h, base[2])
    px, py, pz = quat_rotate(counter_rot, delta)
    return {
        "name": system_name,
        "uuid": make_uuid(),
        "pos": [px, py, pz, 1.0],
        "rot": counter_rot,
        "scl": [1.0, 1.0, 1.0, 1.0],
        # Local AABB covering spawn + drift (fire: ~1.2m up; smoke: ~3m
        # up). Frustum culling tests the node AABB — a spawn-point-sized
        # box makes particles vanish when the camera orbits. The engine
        # additionally expands this from the system's real extent at
        # resolve time (ParticleRenderer::ResolveSystem).
        "aabb": [-0.6, -0.3, -0.6, 0.6, 1.6, 0.6] if system_name == "FireEmber" else [-1.0, -0.5, -1.0, 1.0, 4.5, 1.0],
        "components": [
            {
                "type": "ParticleRenderer",
                "name": system_name,
                "system": system_name,
                "material": "Material_Fire" if system_name == "FireEmber" else "Material_Smoke",
                "blendMode": 1 if system_name == "FireEmber" else 0,
            },
        ],
        "childs": [],
    }


def find_fire_node(node):
    if node.get("name") == "Fire":
        return node
    for child in node.get("childs", []):
        hit = find_fire_node(child)
        if hit is not None:
            return hit
    return None


def strip_existing_particles(scene: dict) -> None:
    """Remove prior FireEmber / SmokePlume from materials / textures /
    particleSystems / scene nodes so a re-run is idempotent."""

    by_name = {t.get("name"): t for t in scene.get("textures", [])}
    for name in ("fire.png", "smoke.png"):
        if name in by_name:
            old_uuid = by_name[name].get("uuid")
            scene["textures"] = [t for t in scene["textures"] if t is not by_name[name]]
            for mat in scene.get("materials", []):
                if isinstance(mat.get("textures"), list):
                    mat["textures"] = [t for t in mat["textures"]
                                       if t.get("name") != name
                                       and (old_uuid is None or t.get("uuid") != old_uuid)]

    scene["materials"] = [m for m in scene.get("materials", [])
                           if m.get("name") not in ("Material_Fire", "Material_Smoke")]

    scene["particleSystems"] = [p for p in scene.get("particleSystems", [])
                                 if p.get("name") not in ("FireEmber", "SmokePlume")]


def strip_existing_emitter_nodes(root: dict) -> None:
    """Strip any prior FireEmber / SmokePlume children of the Fire node."""
    fire_node = find_fire_node(root)
    if fire_node is None:
        return
    fire_node["childs"] = [c for c in fire_node.get("childs", [])
                           if c.get("name") not in ("FireEmber", "SmokePlume")]


def main() -> int:
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else (
        REPO_ROOT / "examples" / "Projects" / "outdoor" / "outdoor_water.json")
    if not src.exists():
        print(f"missing input: {src}", file=sys.stderr)
        return 1
    with src.open() as f:
        scene = json.load(f)

    strip_existing_particles(scene)

    # 1. textures
    scene.setdefault("textures", [])
    fire_tex = fire_texture_entry()
    scene["textures"].append(fire_tex)
    smoke_tex = smoke_texture_entry()
    scene["textures"].append(smoke_tex)

    # 2. materials
    scene.setdefault("materials", [])
    scene["materials"].append(fire_material_entry(fire_tex))
    scene["materials"].append(smoke_material_entry(smoke_tex))

    # 3. particle systems (top-level assets)
    scene.setdefault("particleSystems", [])
    scene["particleSystems"].append(fire_particle_system_entry())
    scene["particleSystems"].append(smoke_particle_system_entry())

    # 4. scene nodes — add FireEmber + SmokePlume as children of "Fire".
    # The visible crossed-logs pile is the "Feu" MESH node; the "Fire"
    # node only carries the point light, ~84cm above it. Parent the
    # emitters to Fire (so they follow the light) but place them at the
    # Feu node's position (pile top), counter-rotated so their local +Y
    # is world-up (particles simulate in owner-local space).
    root = scene["nodes"]
    fire_node = find_fire_node(root)
    if fire_node is None:
        print("could not find 'Fire' node in scene tree", file=sys.stderr)
        return 1

    def find_named(node, name):
        if node.get("name") == name:
            return node
        for child in node.get("childs", []):
            hit = find_named(child, name)
            if hit is not None:
                return hit
        return None

    feu_node = find_named(root, "Feu")
    if feu_node is None:
        print("could not find 'Feu' node in scene tree", file=sys.stderr)
        return 1

    q = fire_node.get("rot", [0.0, 0.0, 0.0, 1.0])
    counter_rot = [-q[0], -q[1], -q[2], q[3]]

    fire_pos = fire_node.get("pos", [0.0, 0.0, 0.0])
    feu_pos = feu_node.get("pos", [0.0, 0.0, 0.0])
    feu_scl = feu_node.get("scl", [1.0, 1.0, 1.0])
    aabb = feu_node.get("aabb", [0.0, 0.0, 0.0, 0.0, 0.0, 0.0])
    # Anchor at the pile's AABB CENTER (mesh-local center × node scale +
    # pivot), not the Feu pivot — the pivot sits at one end of the ~1.7m
    # log spread, which placed the flame off the visible pile.
    anchor = [
        feu_pos[i] + (aabb[i] + aabb[i + 3]) * 0.5 * feu_scl[i]
        for i in range(3)
    ]
    base = [anchor[i] - fire_pos[i] for i in range(3)]

    fire_node.setdefault("childs", [])
    strip_existing_emitter_nodes(root)
    # The emitter pos is in Fire-LOCAL space: world = fire_pos + R×pos.
    # So the whole world-space delta (anchor + h·up − fire_pos) must be
    # rotated by counter_rot (R⁻¹), not just the h part — adding an
    # unrotated base dropped the flame ~50cm off the pile, inside the
    # fence geometry (read as "fire renders behind opaque objects").
    fire_node["childs"].append(emitter_node("FireEmber", counter_rot, base, 0.17))
    fire_node["childs"].append(emitter_node("SmokePlume", counter_rot, base, 0.7))

    # 5. bump version (kFormatVersion=3 today).
    scene["version"] = max(3, int(scene.get("version", 1)))

    with src.open("w") as f:
        json.dump(scene, f, indent=2)
    print(f"wrote {src}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
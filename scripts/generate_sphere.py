#!/usr/bin/env python3
"""Generate a UV sphere mesh as an OBJ file."""

import math
import os

def generate_sphere(radius=1.0, sectors=32, stacks=16, output_path="resource/models/sphere.obj"):
    """
    Generate a UV sphere using latitude-longitude parameterization.

    Args:
        radius: Sphere radius
        sectors: Number of horizontal segments
        stacks: Number of vertical segments
        output_path: Output file path
    """
    vertices = []
    texcoords = []
    normals = []
    faces = []

    # Generate vertices
    for i in range(stacks + 1):
        stack_angle = math.pi / 2 - i * math.pi / stacks
        xy = radius * math.cos(stack_angle)
        z = radius * math.sin(stack_angle)

        for j in range(sectors + 1):
            sector_angle = j * 2 * math.pi / sectors

            x = xy * math.cos(sector_angle)
            y = xy * math.sin(sector_angle)

            # Vertex position
            vertices.append((x, y, z))

            # Normal (same as position for unit sphere)
            nx = x / radius
            ny = y / radius
            nz = z / radius
            normals.append((nx, ny, nz))

            # Texture coordinate
            u = j / sectors
            v = i / stacks
            texcoords.append((u, v))

    # Generate faces
    for i in range(stacks):
        k1 = i * (sectors + 1)
        k2 = k1 + sectors + 1

        for j in range(sectors):
            if i != 0:
                # First triangle of quad
                faces.append((k1 + j, k2 + j, k1 + j + 1))

            if i != (stacks - 1):
                # Second triangle of quad
                faces.append((k1 + j + 1, k2 + j, k2 + j + 1))

    # Create output directory if it doesn't exist
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    # Write OBJ file
    with open(output_path, 'w') as f:
        f.write("# UV Sphere generated mesh\n")
        f.write(f"# Radius: {radius}, Sectors: {sectors}, Stacks: {stacks}\n\n")

        # Write vertices
        for v in vertices:
            f.write(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n")

        f.write("\n")

        # Write texture coordinates
        for t in texcoords:
            f.write(f"vt {t[0]:.6f} {t[1]:.6f}\n")

        f.write("\n")

        # Write normals
        for n in normals:
            f.write(f"vn {n[0]:.6f} {n[1]:.6f} {n[2]:.6f}\n")

        f.write("\n")

        # Write faces
        for face in faces:
            v1, v2, v3 = face
            # OBJ indices are 1-based
            f.write(f"f {v1+1}/{v1+1}/{v1+1} {v2+1}/{v2+1}/{v2+1} {v3+1}/{v3+1}/{v3+1}\n")

    print(f"Generated sphere mesh: {output_path}")
    print(f"Vertices: {len(vertices)}, Faces: {len(faces)}")

if __name__ == "__main__":
    generate_sphere()

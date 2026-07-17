#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;
#ifdef SKINNED_MESH
in ivec4 bone_ids;
in vec3 bone_weights;
uniform mat4 bone_matrices[35];
#endif

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;
uniform mat4 world_matrix;

void main()
{
#ifdef SKINNED_MESH
	mat4 bone_matrix = bone_matrices[bone_ids[0]] * bone_weights[0];
	bone_matrix += bone_matrices[bone_ids[1]] * bone_weights[1];
	bone_matrix += bone_matrices[bone_ids[2]] * bone_weights[2];
	bone_matrix += bone_matrices[bone_ids[3]] * (1.0 - bone_weights[0] - bone_weights[1] - bone_weights[2]);
	gl_Position = projection_matrix * invert_view_matrix * world_matrix * bone_matrix * vec4(vertex_position, 1.0);
#else
	gl_Position = projection_matrix * invert_view_matrix * world_matrix * vec4(vertex_position, 1.0);
#endif
}

#endif

#ifdef FRAGMENT_SHADER

void main()
{
	// gl_FragDepth = gl_FragCoord.z;
}

#endif

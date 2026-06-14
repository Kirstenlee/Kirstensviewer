
uniform mat4 modelview_matrix;
uniform mat4 texture_matrix0;
uniform mat4 modelview_projection_matrix;

in vec3 position;
in vec2 texcoord0;
in vec2 texcoord1;

out vec2 vary_texcoord0;
out vec2 vary_texcoord1;
out vec3 vary_position;

#ifdef HAS_SKIN
mat4 getObjectSkinnedTransform();
uniform mat4 projection_matrix;
#endif

void main()
{
	//transform vertex
#ifdef HAS_SKIN
    mat4 mat = getObjectSkinnedTransform();
    mat = modelview_matrix * mat;
    vec4 pos = mat * vec4(position.xyz, 1.0);
    vary_position = pos.xyz;
    gl_Position = projection_matrix * pos;
#else
    vary_position = (modelview_matrix * vec4(position.xyz, 1.0)).xyz;
	gl_Position = modelview_projection_matrix*vec4(position.xyz, 1.0);
#endif
	vary_texcoord0 = (texture_matrix0 * vec4(texcoord0,0,1)).xy;
	vary_texcoord1 = (texture_matrix0 * vec4(texcoord1,0,1)).xy;
}

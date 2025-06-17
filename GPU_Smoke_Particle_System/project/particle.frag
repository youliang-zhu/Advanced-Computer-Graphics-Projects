#version 420
in float life;
uniform float screen_x;
uniform float screen_y;
layout(binding = 0) uniform sampler2D colortexture;
layout(location = 0) out vec4 fragmentColor;

void main()
{
	// Base color.
	fragmentColor = texture2D(colortexture, gl_PointCoord);
	float brightness = 0.3 * fragmentColor.r + 0.6 * fragmentColor.g + 0.1 * fragmentColor.b;
	fragmentColor.rgb = vec3(brightness, brightness, brightness);
	// Make it darker the older it is.
	//fragmentColor.rgb *= (1.0 - life);
	fragmentColor.rgb *= 0.5;
	// Make it fade out the older it is, also all particles have a
	// very low base alpha so they blend together.
	fragmentColor.a *= (1.0 - pow(life, 4.0)) * 0.05;
}

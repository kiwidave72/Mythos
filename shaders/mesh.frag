#version 330 core

in vec3 vNormal;
in vec3 vFragPos;

uniform vec3 uColor;
uniform vec3 uLightPos;
uniform vec3 uViewPos;

out vec4 FragColor;

void main()
{
    vec3 norm     = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vFragPos);

    // Ambient
    float ambient = 0.25;

    // Diffuse
    float diff    = max(dot(norm, lightDir), 0.0);

    // Specular (blinn-phong)
    vec3  viewDir = normalize(uViewPos - vFragPos);
    vec3  halfDir = normalize(lightDir + viewDir);
    float spec    = pow(max(dot(norm, halfDir), 0.0), 32.0) * 0.4;

    vec3 result = (ambient + diff + spec) * uColor;
    FragColor   = vec4(result, 1.0);
}

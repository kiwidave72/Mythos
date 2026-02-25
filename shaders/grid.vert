#version 330 core
layout(location = 0) in vec2 aPos;

out vec3 vNear;
out vec3 vFar;

uniform mat4 uInvVP;

vec3 unproject(vec2 xy, float z) {
    vec4 h = uInvVP * vec4(xy, z, 1.0);
    return h.xyz / h.w;
}

void main() {
    vNear = unproject(aPos, -1.0);
    vFar  = unproject(aPos,  1.0);
    gl_Position = vec4(aPos, 0.0, 1.0);
}

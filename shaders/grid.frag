#version 330 core
in vec3 vNear;
in vec3 vFar;

out vec4 FragColor;

uniform float uCamDist;
uniform vec3  uCamPos;
uniform mat4  uVP;

float hash(vec2 c) {
    return fract(sin(dot(c, vec2(127.1, 311.7))) * 43758.5453);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i+vec2(1,0)), u.x),
               mix(hash(i+vec2(0,1)), hash(i+vec2(1,1)), u.x), u.y);
}

float fbm(vec2 p) {
    return 0.500 * valueNoise(p)
         + 0.250 * valueNoise(p * 2.1)
         + 0.125 * valueNoise(p * 4.3);
}

float gridLine(vec2 p, float cellSize) {
    vec2 wrapped = abs(fract(p / cellSize + 0.5) - 0.5) * cellSize;
    vec2 cover   = smoothstep(fwidth(p), vec2(0.0), wrapped);
    return max(cover.x, cover.y);
}

void main() {
    float denom = vFar.y - vNear.y;
    if (abs(denom) < 1e-6) discard;
    float t = -vNear.y / denom;
    if (t < 0.0) discard;

    vec3 hit = vNear + t * (vFar - vNear);
    vec2 p   = hit.xz;

    vec4 clip = uVP * vec4(hit, 1.0);
    gl_FragDepth = (clip.z / clip.w) * 0.5 + 0.5;

    float log10D   = log(uCamDist) / log(10.0);
    float cellBase = pow(10.0, floor(log10D) - 1.0);
    float blend    = fract(log10D);

    float g0 = gridLine(p, cellBase * 1000.0);
    float g1 = gridLine(p, cellBase * 100.0);
    float g2 = gridLine(p, cellBase * 10.0);
    float g3 = gridLine(p, cellBase) * (1.0 - smoothstep(0.5, 1.0, blend));

    float axisX = smoothstep(fwidth(p.y)*2.0, 0.0, abs(p.y));
    float axisZ = smoothstep(fwidth(p.x)*2.0, 0.0, abs(p.x));

    float fade = 1.0 - smoothstep(uCamDist*1.5, uCamDist*7.0,
                                   length(p - uCamPos.xz));
    if (fade < 0.001) discard;

    // --- concrete tile texture ---
    float tileSize  = cellBase * 10.0;
    vec2  cellUV    = fract(p / tileSize);
    vec2  cellID    = floor(p / tileSize);
    float edgeDist  = max(abs(cellUV.x - 0.5), abs(cellUV.y - 0.5)) * 2.0;
    float groutMask = smoothstep(0.92, 1.0, edgeDist);
    float edgeWeight = smoothstep(0.55, 1.0, edgeDist);
    float ns        = 4.0 / tileSize;
    float interior  = fbm(p * ns + cellID * 7.3);
    float edgeNoise = fbm(p * ns * 3.0 + cellID * 13.7);
    float noiseMix  = mix(interior * 0.12, edgeNoise * 0.55, edgeWeight);
    float concrete  = clamp(hash(cellID) * 0.82 + 0.5 * 0.82 + noiseMix, 0.0, 1.0);
    float grout     = clamp(edgeNoise * 0.4 + 0.08, 0.0, 1.0);
    float surface   = mix(concrete, grout, groutMask);
    vec3  surfaceCol = mix(vec3(0.28,0.33,0.42), vec3(0.62,0.68,0.78), surface);

    vec3  col   = surfaceCol;
    float alpha = 0.72;

    col = mix(col, vec3(0.19,0.20,0.23), g3); alpha = max(alpha, g3);
    col = mix(col, vec3(0.24,0.25,0.29), g2); alpha = max(alpha, g2);
    col = mix(col, vec3(0.30,0.31,0.36), g1); alpha = max(alpha, g1);
    col = mix(col, vec3(0.38,0.39,0.43), g0); alpha = max(alpha, g0);
    col = mix(col, vec3(0.70,0.14,0.14), axisX); alpha = max(alpha, axisX);
    col = mix(col, vec3(0.14,0.30,0.70), axisZ); alpha = max(alpha, axisZ);

    alpha *= fade;
    if (alpha < 0.005) discard;

    FragColor = vec4(col, alpha);
}

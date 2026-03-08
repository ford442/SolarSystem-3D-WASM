#version 300 es

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;

out vec3 vFragPos;
out vec2 vTexCoords;
out vec3 vTangentLightPos;
out vec3 vTangentViewPos;
out vec3 vTangentFragPos;
out vec4 vFragPosLightSpace;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
uniform mat4 lightSpaceMatrix;

uniform vec3 lightPos;
uniform vec3 viewPos;

uniform float zCoef; // For log z-buffer (2.0 / log2(farPlane + 1.0))

void main() {
    vFragPos = vec3(model * vec4(aPos, 1.0));
    vTexCoords = aTexCoords;

    mat3 normalMatrix = mat3(transpose(inverse(model)));
    vec3 T = normalize(normalMatrix * aTangent);
    vec3 N = normalize(normalMatrix * aNormal);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);

    mat3 TBN = transpose(mat3(T, B, N));

    vTangentLightPos = TBN * lightPos;
    vTangentViewPos  = TBN * viewPos;
    vTangentFragPos  = TBN * vFragPos;
    vFragPosLightSpace = lightSpaceMatrix * vec4(vFragPos, 1.0);

    gl_Position = projection * view * vec4(vFragPos, 1.0f);

    // Log z-buffer [логарифмический z-буфер]
    gl_Position.z = log2(max(1e-6, gl_Position.w + 1.0)) * zCoef - 1.0;
    gl_Position.z *= gl_Position.w;
}
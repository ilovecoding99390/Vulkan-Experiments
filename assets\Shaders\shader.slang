// based on Sebastian Lague's and How to Vulkan's

struct VSInput {
    float3 Pos;
    float3 Normal;
	float2 UV;
};


struct extragpudata {
    float4 bounds_min;
    float4 bounds_max;

    float4 offset;

    float4 rotation;

    float4 scale;

};

struct VSOutput {
    float4 Pos : SV_POSITION;
    float3 Normal;
};

float3x3 rotateX(float rad) {
    float c = cos(rad), s = sin(rad);
    return float3x3(1, 0, 0, 0, c, -s, 0, s, c);
}
float3x3 rotateY(float rad) {
    float c = cos(rad), s = sin(rad);
    return float3x3(c, 0, s, 0, 1, 0, -s, 0, c);
}
float3x3 rotateZ(float rad) {
    float c = cos(rad), s = sin(rad);
    return float3x3(c, -s, 0, s, c, 0, 0, 0, 1);
}

float3x3 eulerToMatrix(float3 angles) {
    return mul(mul(rotateX(angles.x), rotateY(angles.y)), rotateZ(angles.z));
}

[shader("vertex")]
VSOutput VSMain(uniform extragpudata data, VSInput input) {
    float3 midpoint = (data.bounds_max.xyz + data.bounds_min.xyz) / 2.0;
    float3 scale = data.bounds_max.xyz - midpoint;
    float truescale = 2 * max(max(scale[0], scale[1]), scale[2]);
    float3x3 RotationMatrix = eulerToMatrix(data.rotation.xyz);

    VSOutput o;
    o.Pos = float4(mul((input.Pos.xyz - midpoint) / truescale, RotationMatrix)*data.scale.xyz + data.offset.xyz, 1.0);
    o.Pos.z /= 2;
    o.Pos.z += 0.5;
    o.Pos.y *= -1;
    o.Normal = input.Normal;
    o.Pos.w = o.Pos.z;
    return o;
}

[shader("fragment")]
float4 FRMain(VSOutput input) {
    float3 normal = normalize(input.Normal);
    float3 col = normal / 2.0 + 0.5;
    return float4(pow(col, 2.2), 1.0);
}

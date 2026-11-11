struct InstanceData
{
	row_major float4x4 world;
	float4 boundingSphere;
	uint meshID;
	uint boneOffset;
	uint isSkinned;
	uint textureIndex;
	uint paramOffset;
	uint3 _pad;
};

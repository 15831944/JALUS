#include "SkillIndex.h"

void SkillIndex::writeToBitStream(BitStream* bitStream, bool isConstruction)
{
	if (isConstruction)
	{
		bitStream->Write(flag_0);
	}
}

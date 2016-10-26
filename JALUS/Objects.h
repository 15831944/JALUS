#pragma once
#include "Common.h"

class Objects
{
private:
	static bool validate(long long objectID);
public:
	static string name;

	static void init(string name, string structure);

	static long long generateObjectID();
	static long long generateSpawnerID();

	static long long createObject(long lot);
	static void deleteObject(long long objectID);
};
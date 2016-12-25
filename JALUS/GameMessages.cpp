#include "GameMessages.h"
#include "Logger.h"
#include "Objects.h"
#include "Server.h"
#include "PacketUtils.h"
#include "ObjectsManager.h"
#include "CDClient.h"
#include "Missions.h"
#include "Sessions.h"
#include "Chat.h"
#include "LUZCache.h"
#include "Flags.h"
#include "Characters.h"

void GameMessages::processGameMessage(BitStream* data, SystemAddress clientAddress)
{
	Session* session = Sessions::getSession(clientAddress);

	if (session != nullptr)
	{
		long long objectID;
		data->Read(objectID);
		unsigned short gameMessageID;
		data->Read(gameMessageID);

		switch (gameMessageID)
		{

		case GAME_MESSAGE_ID_READY_FOR_UPDATES:
		{
			long long ready;
			data->Read(ready);
			Logger::info("Object is ready for updates! (Object: " + to_string(ready) + ") (Address: " + string(clientAddress.ToString()) + ")");

			long lot = Objects::getLOT(ready);
			ReplicaObject* replica = ObjectsManager::getObjectByID(session->charID);

			if (lot > -1)
			{
				if (lot == 1)
				{
					Server::sendPacket(PacketUtils::createGMBase(ready, 1642), clientAddress);
					Server::sendPacket(PacketUtils::createGMBase(ready, 509), clientAddress);

					BitStream* flagChange = PacketUtils::createGMBase(ready, 472);
					flagChange->Write((unsigned char)185);
					flagChange->Write((unsigned long)0);
					Server::sendPacket(flagChange, clientAddress);

					//BitStream* enableJetpack = PacketUtils::createGMBase(ready, 561);
					//enableJetpack->Write(true);
					//enableJetpack->Write(false);
					//enableJetpack->Write(true);
					//enableJetpack->Write(false); // effectID
					//enableJetpack->Write(false); // fAirspeed
					//enableJetpack->Write(false); // fMaxAirspeed
					//enableJetpack->Write(false); // fVertVel
					//enableJetpack->Write(false); // iWarningEffectID
					//Server::sendPacket(enableJetpack, clientAddress);

					Server::sendPacket(PacketUtils::createGMBase(session->charID, GameMessageID::GAME_MESSAGE_ID_RESTORE_TO_POST_LOAD_STATS), clientAddress);

					vector<MissionInfo> misCur = Missions::getAllCurrentMissions(session->charID);
					if (misCur.size() > 0)
					{
						for (int i = 0; i < misCur.size(); i++)
						{
							MissionInfo info = misCur.at(i);

							MissionInfo newInfo = MissionInfo();
							newInfo.missionID = info.missionID;

							vector<MissionTask> tasksCur = CDClient::getMissionTasks(info.missionID);

							for (int k = 0; k < info.missionTasks.size(); k++)
							{
								MissionTask withValue = info.missionTasks.at(k);

								for (int l = 0; l < tasksCur.size(); l++)
								{
									MissionTask withInfo = tasksCur.at(l);

									if (withValue.uid == withInfo.uid)
									{
										withInfo.value = withValue.value;
										newInfo.missionTasks.push_back(withInfo);

										GameMessages::notifyMission(session->charID, info.missionID, MissionState::MISSION_STATE_ACTIVE, false, clientAddress);

										for (int m = 0; m < withInfo.value; m++)
										{
											vector<float> updates = vector<float>();
											updates.push_back((float)(m + 1));

											GameMessages::notifyMissionTask(session->charID, info.missionID, k, updates, clientAddress);
										}

										if (withInfo.value >= withInfo.targetValue)
										{
											if (CDClient::isMission(info.missionID))
											{
												GameMessages::notifyMission(session->charID, info.missionID, MissionState::MISSION_STATE_READY_TO_COMPLETE, true, clientAddress);
											}
											else
											{
												Missions::completeMission(info.missionID, session->charID, clientAddress);
											}
										}
									}
								}
							}

							replica->currentMissions.push_back(newInfo);
						}
					}

					vector<Flag> flags = Flags::getFlags(session->charID);
					for (int i = 0; i < flags.size(); i++)
					{
						Flag f = flags.at(i);

						GameMessages::notifyClientFlagChange(session->charID, f.flagID, f.value, clientAddress);
					}
				}
			}
			break;
		}

		case GAME_MESSAGE_ID_REQUEST_USE:
		{
			bool isMultiInteractUse;
			data->Read(isMultiInteractUse);
			long multiInteractID;
			data->Read(multiInteractID);
			MultiInteractType multiInteractType;
			data->Read(multiInteractType);
			long long objectID;
			data->Read(objectID);

			Missions::callOnMissionTaskUpdate(MissionTaskType::MISSION_TASK_TYPE_INTERACT, session->charID, objectID, clientAddress);

			if (isMultiInteractUse)
			{
				switch (multiInteractType)
				{

				case MULTI_INTERACT_TYPE_MISSION:
				{
					GameMessages::offerMission(session->charID, multiInteractID, objectID, clientAddress);
					break;
				}

				default:
					break;

				}
			}
			else
			{
				long lot = ObjectsManager::getObjectByID(objectID)->lot;
				vector<MissionNPCInfo> mnpc = CDClient::getMissionNPCIndexInfo(lot);

				for (int i = 0; i < mnpc.size(); i++)
				{
					MissionNPCInfo info = mnpc.at(i);

					if (info.acceptsMission)
					{
						if (Missions::isDoingMission(info.missionID, session->charID))
						{
							GameMessages::offerMission(session->charID, info.missionID, objectID, clientAddress, false);
						}
					}
				}

				for (int i = 0; i < mnpc.size(); i++)
				{
					MissionNPCInfo info = mnpc.at(i);

					if (info.offersMission)
					{
						if (!Missions::hasDoneMission(info.missionID, session->charID))
						{
							vector<long> prereq = CDClient::getPrereqMissionIDs(info.missionID);
							bool ready = true;

							for (int k = 0; k < prereq.size(); k++)
							{
								if (ready)
									ready = Missions::hasDoneMission(prereq.at(k), session->charID);
							}

							if (ready)
								GameMessages::offerMission(session->charID, info.missionID, objectID, clientAddress);
						}
					}
				}
			}
			break;
		}

		case GAME_MESSAGE_ID_MISSION_DIALOGUE_OK:
		{
			bool isComplete;
			data->Read(isComplete);
			MissionState missionState;
			data->Read(missionState);
			long missionID;
			data->Read(missionID);
			long long responder;
			data->Read(responder);

			Logger::info("MissionDialogueOK received! {");
			Logger::info("   objectID: " + to_string(objectID));
			Logger::info("   isComplete: " + to_string(isComplete));
			Logger::info("   missionState: " + to_string(missionState));
			Logger::info("   missionID: " + to_string(missionID));
			Logger::info("   responder: " + to_string(responder));
			Logger::info("}");

			if (!Missions::isDoingMission(missionID, session->charID))
			{
				Missions::addMission(missionID, session->charID);
				vector<MissionTask> tasks = CDClient::getMissionTasks(missionID);

				MissionInfo info = MissionInfo();
				info.missionID = missionID;

				for (int i = 0; i < tasks.size(); i++)
				{
					MissionTask task = tasks.at(i);

					CurrentMissionTasks::addMissionTask(missionID, task.uid, session->charID);
					info.missionTasks.push_back(task);
				}

				ObjectsManager::getObjectByID(session->charID)->currentMissions.push_back(info);

				GameMessages::notifyMission(session->charID, missionID, MissionState::MISSION_STATE_ACTIVE, isComplete, clientAddress);
			}
			else
			{
				if (isComplete)
				{
					Missions::completeMission(missionID, session->charID, clientAddress);
				}
			}

			long lot = ObjectsManager::getObjectByID(objectID)->lot;
			vector<MissionNPCInfo> mnpc = CDClient::getMissionNPCIndexInfo(lot);

			for (int i = 0; i < mnpc.size(); i++)
			{
				MissionNPCInfo info = mnpc.at(i);

				if (info.acceptsMission)
				{
					if (Missions::isDoingMission(info.missionID, session->charID))
					{
						GameMessages::offerMission(session->charID, info.missionID, objectID, clientAddress);
					}
				}
			}

			for (int i = 0; i < mnpc.size(); i++)
			{
				MissionNPCInfo info = mnpc.at(i);

				if (info.offersMission)
				{
					if (!Missions::hasDoneMission(info.missionID, session->charID))
					{
						vector<long> prereq = CDClient::getPrereqMissionIDs(info.missionID);
						bool ready = true;

						for (int k = 0; k < prereq.size(); k++)
						{
							if (ready)
								ready = Missions::hasDoneMission(prereq.at(k), session->charID);
						}

						if (ready)
							GameMessages::offerMission(session->charID, info.missionID, objectID, clientAddress);
					}
				}
			}
			break;
		}

		case GAME_MESSAGE_ID_HAS_BEEN_COLLECTED:
		{
			long long playerID;
			data->Read(playerID);

			if (playerID == session->charID)
			{
				Missions::callOnMissionTaskUpdate(MissionTaskType::MISSION_TASK_TYPE_COLLECT_COLLECTIBLE, session->charID, objectID, clientAddress);
			}
			break;
		}

		case GAME_MESSAGE_ID_REQUEST_DIE:
		{
			for (int i = 0; i < Server::getReplicaManager()->GetParticipantCount(); i++)
			{
				SystemAddress participant = Server::getReplicaManager()->GetParticipantAtIndex(i);

				GameMessages::die(session->charID, L"electro-shock-death", false, participant);
			}
			break;
		}

		case GAME_MESSAGE_ID_REQUEST_RESURRECT:
		{
			Position spawnPos = LUZCache::getByZoneID(ServerRoles::toZoneID(Server::getServerRole()))->spawnPointPos;
			Rotation spawnRot = LUZCache::getByZoneID(ServerRoles::toZoneID(Server::getServerRole()))->spawnPointRot;

			for (int i = 0; i < Server::getReplicaManager()->GetParticipantCount(); i++)
			{
				SystemAddress participant = Server::getReplicaManager()->GetParticipantAtIndex(i);

				GameMessages::teleport(session->charID, false, false, true, true, spawnPos, participant, spawnRot);
				GameMessages::resurrect(session->charID, false, participant);
			}
			break;
		}

		case GAME_MESSAGE_ID_SET_FLAG:
		{
			bool value;
			data->Read(value);
			long flagID;
			data->Read(flagID);

			if (objectID == session->charID)
			{
				Flags::setFlagValue(value, flagID, session->charID);
				Missions::callOnMissionTaskUpdate(MissionTaskType::MISSION_TASK_TYPE_FLAG_CHANGE, session->charID, flagID, clientAddress);
			}
			break;
		}

		case GAME_MESSAGE_ID_NOTIFY_SERVER_LEVEL_PROCESSING_COMPLETE:
		{
			Characters::setLevel(Characters::getLevel(session->charID) + 1, session->charID);
			break;
		}

		case GAME_MESSAGE_ID_RESPOND_TO_MISSION:
		{
			long missionID;
			data->Read(missionID);
			long long playerID;
			data->Read(playerID);
			long long receiver;
			data->Read(receiver);
			long rewardLOT;
			data->Read(rewardLOT);

			Logger::info("RespondToMission received! {");
			Logger::info("   objectID: " + to_string(objectID));
			Logger::info("   missionID: " + to_string(missionID));
			Logger::info("   playerID: " + to_string(playerID));
			Logger::info("   receiver: " + to_string(receiver));
			Logger::info("   rewardLOT: " + to_string(rewardLOT));
			Logger::info("}");
			break;
		}

		default:
		{
			Logger::info("Server received an unknown GameMessage! (GameMessageID: " + to_string(gameMessageID) + ") (ObjectID: " + to_string(objectID) + ") (Address: " + string(clientAddress.ToString()) + ")");
			break;
		}

		}
	}
}

void GameMessages::offerMission(long long objectID, long missionID, long long offererID, SystemAddress receiver, bool sendTwice)
{
	BitStream* packet = PacketUtils::createGMBase(objectID, GameMessageID::GAME_MESSAGE_ID_OFFER_MISSION);

	packet->Write(missionID);
	packet->Write(offererID);

	Server::sendPacket(packet, receiver);

	if (sendTwice)
	{
		BitStream* packet2 = PacketUtils::createGMBase(offererID, GameMessageID::GAME_MESSAGE_ID_OFFER_MISSION);

		packet2->Write(missionID);
		packet2->Write(offererID);

		Server::sendPacket(packet2, receiver);
	}
}

void GameMessages::notifyMission(long long objectID, long missionID, MissionState missionState, bool sendingRewards, SystemAddress receiver)
{
	BitStream* packet = PacketUtils::createGMBase(objectID, GameMessageID::GAME_MESSAGE_ID_NOTIFY_MISSION);

	packet->Write(missionID);
	packet->Write(missionState);
	packet->Write(sendingRewards);

	Server::sendPacket(packet, receiver);
}

void GameMessages::notifyMissionTask(long long objectID, long missionID, long taskIndex, vector<float> updates, SystemAddress receiver)
{
	BitStream* packet = PacketUtils::createGMBase(objectID, GameMessageID::GAME_MESSAGE_ID_NOTIFY_MISSION_TASK);

	packet->Write(missionID);
	packet->Write(1 << (taskIndex + 1));
	packet->Write((unsigned char)updates.size());

	for (int i = 0; i < updates.size(); i++)
	{
		packet->Write(updates.at(i));
	}

	Server::sendPacket(packet, receiver);
}

void GameMessages::die(long long objectID, wstring deathType, bool spawnLoot, SystemAddress receiver, long long killerID, long long lootOwnerID)
{
	BitStream* packet = PacketUtils::createGMBase(objectID, GameMessageID::GAME_MESSAGE_ID_DIE);

	packet->Write(true);
	packet->Write(spawnLoot);
	packet->Write(false);
	packet->Write((unsigned long)deathType.length());

	for (int i = 0; i < deathType.length(); i++)
	{
		packet->Write(deathType[i]);
	}

	packet->Write(0.0F);
	packet->Write(0.0F);
	packet->Write(0.0F);
	packet->Write(false);

	if (killerID > 1)
	{
		packet->Write(killerID);
	}
	else
	{
		packet->Write(objectID);
	}

	if (lootOwnerID > 1)
	{
		packet->Write(true);
		packet->Write(lootOwnerID); 
	}
	else
	{
		packet->Write(false);
	}

	Server::sendPacket(packet, receiver);
}

void GameMessages::resurrect(long long objectID, bool rezImmediately, SystemAddress receiver)
{
	BitStream* packet = PacketUtils::createGMBase(objectID, GameMessageID::GAME_MESSAGE_ID_RESURRECT);

	packet->Write(rezImmediately);

	Server::sendPacket(packet, receiver);
}

void GameMessages::teleport(long long objectID, bool noGravTeleport, bool ignoreY, bool setRotation, bool skipAllChecks, Position pos, SystemAddress receiver, Rotation rot)
{
	BitStream* packet = PacketUtils::createGMBase(objectID, GameMessageID::GAME_MESSAGE_ID_TELEPORT);

	packet->Write(noGravTeleport);
	packet->Write(ignoreY);
	packet->Write(setRotation);
	packet->Write(skipAllChecks);
	packet->Write(pos.x);
	packet->Write(pos.y);
	packet->Write(pos.z);

	if (setRotation)
	{
		packet->Write(rot.w);
		packet->Write(rot.x);
		packet->Write(rot.y);
		packet->Write(rot.z);
	}

	Server::sendPacket(packet, receiver);
}

void GameMessages::modifyLegoScore(long long objectID, long long score, bool updateScoreBar, SystemAddress receiver)
{
	BitStream* packet = PacketUtils::createGMBase(objectID, GameMessageID::GAME_MESSAGE_ID_MODIFY_LEGO_SCORE);

	packet->Write(score);

	if (updateScoreBar)
	{
		packet->Write((long)129);
		packet->Write((unsigned char)0);
	}
	else
	{
		packet->Write(true);
		packet->Write((unsigned long)0);
	}

	Server::sendPacket(packet, receiver);
}

void GameMessages::setCurrency(long long objectID, long long currency, Position pos, SystemAddress receiver)
{
	BitStream* packet = PacketUtils::createGMBase(objectID, GameMessageID::GAME_MESSAGE_ID_SET_CURRENCY);

	packet->Write(currency);
	packet->Write(false);
	packet->Write(pos.x);
	packet->Write(pos.y);
	packet->Write(pos.z);
	packet->Write(false);
	packet->Write(false);
	packet->Write(false);
	packet->Write(false);

	Server::sendPacket(packet, receiver);
}

void GameMessages::notifyClientFlagChange(long long objectID, long flagID, bool value, SystemAddress receiver)
{
	BitStream* packet = PacketUtils::createGMBase(objectID, GameMessageID::GAME_MESSAGE_ID_NOTIFY_CLIENT_FLAG_CHANGE);

	packet->Write(value);
	packet->Write(flagID);

	Server::sendPacket(packet, receiver);
}

void GameMessages::updateReputation(long long objectID, long long reputation, SystemAddress receiver)
{
	BitStream* packet = PacketUtils::createGMBase(objectID, GameMessageID::GAME_MESSAGE_ID_UPDATE_REPUTATION);

	packet->Write(reputation);

	Server::sendPacket(packet, receiver);
}

void GameMessages::setInventorySize(long long objectID, InventoryType type, long size, SystemAddress receiver)
{
	BitStream* packet = PacketUtils::createGMBase(objectID, GameMessageID::GAME_MESSAGE_ID_SET_INVENTORY_SIZE);

	packet->Write(type);
	packet->Write(size);

	Server::sendPacket(packet, receiver);
}

#include "LoggedUser.h"
#include "../WorldManager.h"
#include "../manager/BSRmbShopManager.h"
#include "../manager/BSAwardManager.h"
#include "../manager/BSCouponManager.h"
#include "../manager/DC_File.h"
#include	"../manager/DC_DataBase.h"
#include "../manager/BSDiamondShop.h"
#include "../manager/BSFactory.h"
#include "../manager/LoggedUserManager.h"
#include "BSTool.h"
#include "../PPLink/PPLinkManger.h"
extern PassPortDatabaseWorkerPool PassPortDatabase;

LoggedUser::LoggedUser(const std::string &uuid) :
		m_uuid(uuid), m_stamp(1), m_bsPlayerBaseInfo(this), m_bsTechUpgrade(
				this), m_bsTowerUpgrade(this), m_bsLevel(this), m_bsGoldMine(
				this), m_bsHero(this), m_bsIntrusion(this), m_bsItemStore(this), m_bsMonsterMap(
				this), m_bsForceGuide(this), m_bsPlatformStore(this), m_bsLevelGroupMgr(
				this), m_bsMission(this), m_bsRegistration(this)
{
	this->strTag = "LoggedUser";
}

LoggedUser::LoggedUser(const UUID &uuid) :
		m_uuid(uuid.to_string()), m_stamp(1), m_bsPlayerBaseInfo(this), m_bsTechUpgrade(
				this), m_bsTowerUpgrade(this), m_bsLevel(this), m_bsGoldMine(
				this), m_bsHero(this), m_bsIntrusion(this), m_bsItemStore(this), m_bsMonsterMap(
				this), m_bsForceGuide(this), m_bsPlatformStore(this), m_bsLevelGroupMgr(
				this), m_bsMission(this), m_bsRegistration(this)
{

}

LoggedUser::~LoggedUser()
{
	if (m_client)
	{
		m_client->setPacketHandler(nullptr);
		m_client->close();
	}
}
PacketPtr LoggedUser::createRequest(u32 pid)
{
	return PacketFactory<LoggedUser>::instance().createRequest(pid);
}

void LoggedUser::_updateStamp()
{
	//std::string strUUId = GetUUId();
	++m_stamp;
	_markDirty();
}

void LoggedUser::_notify_couponUsed()
{
	m_coupon = 0;
	m_couponTimer = sWorldManager.instance().coupon_interval();
}

void LoggedUser::_notify_heroUnlocked(u32 heroClassId)
{
	if (m_coupon && m_coupon->type() == BSCoupon::kCT_Hero)
	{
		auto pc = static_cast<const BSCoupon_Hero *>(m_coupon);
		if (pc->heroClassId == heroClassId)
		{
			_notify_couponUsed();
		}
	}

}

//获取通关奖励
void LoggedUser::_notify_levelPassed(const BSLevel::LevelInfo &info)
{
	if (info.level->f_CouponId.value)
	{
		auto coupon = BSCouponManager::instance().find(this,
				info.level->f_CouponId.value);
		if (coupon)
		{
			m_coupon = coupon;
			m_couponTimer = m_coupon->validTime;
			_sendCouponInfo();
		}
	}
}

void LoggedUser::send_intrusion(u32 levelId, bool group)
{
	_updateStamp();
	if (m_client)
	{
		pkt::res_intrusion msg;
		msg.data.data.levelId = levelId;
		msg.data.data.group = group;
		m_client->sendPacket(msg);
	}
}

void LoggedUser::OtherLogin(const ClientPtr &client)
{
	m_client->setPacketHandler(nullptr);
	pkt::res_otherUserLogin res;
	m_client->sendPacket(res, Session::kClose);
	queue()->runLoop().post(std::bind(&LoggedUser::login, this, client));
}

void LoggedUser::login(const ClientPtr &client)
{
	if (m_client)
	{
		VN_LOG_DEBUG("otherUserLogin");
		m_client->setPacketHandler(nullptr);
		pkt::res_otherUserLogin res;
		m_client->sendPacket(res, Session::kClose);
	} VN_LOG_DEBUG("uuid:"<<m_uuid<<"send res_login ok");
	m_client = client;
	m_client->setPacketHandler(this);

	pkt::res_loginOk msg;
	msg.data.data = m_stamp;
	m_client->sendPacket(msg);
	_generateCoupon();
	_openLottery();
	//todo
	//记录日志
	//DataCenter::instance().log_user_login(m_uuid);
	auto log = vnnew DC_DataBase_EventLog_User_Login;
	log->strUUId = GetUUId();
	UserEventLogPtr logPtr(log);
	logPtr->Asyncsave( { this, true }, [](bool ret)
	{
		if(!ret)
		{
			VN_LOG_ERROR("DC_DataBase_EventLog_User_Login save failed");
		}
	});
	/* ((DC_DataBase *)&DataCenter::instance())->AsyncLog(this,logPtr,[](bool ret)
	 {
	 if(!ret)
	 {
	 VN_LOG_ERROR("DC_DataBase_EventLog_User_Login save failed");
	 }
	 });*/
}

void LoggedUser::signin(const ClientPtr &client)
{
	m_client = client;
	m_client->setPacketHandler(this);
	//reset_data();

	pkt::res_initPlayerData pkt;
	//pkt.data.data.uid.low = m_uuid.low();
	//pkt.data.data.uid.high = m_uuid.high();

	_buildPlayerData(pkt.data.data.playerData);
	m_client->sendPacket(pkt);

	_sendRegistrationInfo();
	_sendAnnouncementId();

	// DataCenter::instance().log_user_signin(m_uuid);
	auto log = vnnew DC_DataBase_EventLog_User_Signin;
	log->strUUId = GetUUId();
	UserEventLogPtr logPtr(log);
	((DC_DataBase *) &DataCenter::instance())->AsyncLog(this, logPtr,
			[](bool ret)
			{
				if(!ret)
				{
					VN_LOG_ERROR("DC_DataBase_EventLog_User_Signin save failed");
				}
			});
}

void LoggedUser::_closed()
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"closed");
	if (m_client)
	{
		m_client->setPacketHandler(nullptr);
		m_client.reset();
	}
	AsyncTaskPtr taskPtr = vnnew AsyncTask();
	taskPtr->Init(3);
	//保存数据
	m_bsGoldMine.AsyncSave( { this, true }, [this,taskPtr](bool ret)
	{
		if(!ret)
		{
			VN_LOG_ERROR("UUId: "<<m_uuid<<"saveGoldFailed");
		}
		taskPtr->FinishTask(ret);
		_HandlClose(taskPtr);
	});

	/*m_bsIntrusion.Asyncsave({this,true},[this,taskPtr](bool ret)
	 {
	 if(!ret)
	 {
	 VN_LOG_ERROR("UUId: "<<m_uuid.to_string()<<"saveIntrusionFailed");
	 }
	 taskPtr->FinishTask(ret);
	 _HandlClose(taskPtr);
	 });*/

	((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( { this,
			true }, { this, true }, [this,taskPtr](bool ret)
	{
		if(!ret)
		{
			VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
		}
		taskPtr->FinishTask(ret);
		_HandlClose(taskPtr);
	});
	//记入日志
	auto log = vnnew DC_DataBase_EventLog_User_Logout;
	log->strUUId = GetUUId();
	UserEventLogPtr logPtr(log);
	logPtr->Asyncsave( { this, true }, [this,taskPtr](bool ret)
	{
		if(!ret)
		{
			VN_LOG_ERROR("UUid"<<GetUUId()<<"Logout failed");
		}
		taskPtr->FinishTask(ret);
		_HandlClose(taskPtr);
	});
}

//todo
bool LoggedUser::_generateCoupon()
{
	if (m_coupon || m_couponTimer || !m_client)
	{
		return false;
	}
	m_coupon = BSCouponManager::instance().generate(this);
	if (m_coupon)
	{
		m_couponTimer = m_coupon->validTime;
		_markDirty();
		return true;
	}
	return false;
}

bool LoggedUser::_openLottery()
{
	if (m_lotteryState != kLottery_Unlocked)
	{
		return false;
	}
	_markDirty();
	m_lotteryPool = BSAwardManager::instance().generate();
	m_lotteryState = kLottery_Opened;
	m_lotteryTimer = sWorldManager.instance().lottery_validTime();
	//m_lotteryDrawTimes = App::instance().lottery_drawTimes();
	return true;
}

void LoggedUser::_updateCoupon(f32 deltaTime)
{
	if (m_coupon)
	{
		m_couponTimer -= deltaTime;
		if (m_couponTimer <= 0.f)
		{
			m_coupon = 0;
			m_couponTimer = sWorldManager.instance().coupon_interval();
		}
		_markDirty();
	} else if (m_couponTimer)
	{
		m_couponTimer -= deltaTime;
		if (m_couponTimer <= 0.f)
		{
			m_couponTimer = 0;
			if (_generateCoupon())
			{
				_sendCouponInfo();
			}
		}
		_markDirty();
	}
}

void LoggedUser::_checkLottery(u32 levelId)
{
	if (m_lotteryState != kLottery_Locked)
	{
		return;
	}
	if (levelId >= sWorldManager.instance().lottery_beginLevel())
	{
		m_lotteryState = kLottery_Unlocked;
		_openLottery();
		_sendLotteryInfo();
	}
}

/*void LoggedUser::Async_checkLottery(u32 levelId,std::function<void (bool ret)> doResult)
 {
 if (m_lotteryState != kLottery_Locked)
 {
 return doResult(true);
 }
 if (levelId >= sWorldManager.instance().lottery_beginLevel())
 {
 m_lotteryState = kLottery_Unlocked;
 _openLottery();
 //保存彩票信息
 ((DC_DataBase *)&DataCenter::instance())->AsyncUpdateLottery({this,true},{this,true},[this,doResult](bool ret)
 {
 if(!ret)
 {
 VN_LOG_ERROR("UUid:"<<this->GetUUId()<<"AsyncUpdateLottery"<<"FAILED");
 return doResult(false);
 }
 _sendLotteryInfo();
 return	doResult(true);
 });
 }
 else
 {
 return doResult(true);
 }
 }*/

void LoggedUser::_updateLottery(f32 deltaTime)
{
	if (m_lotteryState != kLottery_Opened)
	{
		return;
	}
	_markDirty();
	m_lotteryTimer -= deltaTime;
	if (m_lotteryTimer <= 0.f)
	{
		m_lotteryTimer = 0;
		m_lotteryState = kLottery_Closed;
		m_lotteryPool.clear();
	}
}

void LoggedUser::CalculateCoupon(vn::u64 lastTime)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"lastCouponTimestamp is "<<lastTime);
	vn::u64 curTime = time(NULL);
	vn::u64 timeInterval = curTime - lastTime;

	if (m_coupon)
	{
		m_couponTimer -= timeInterval;
		if (m_couponTimer <= 0.f)
		{
			m_coupon = 0;
			m_couponTimer = sWorldManager.instance().coupon_interval();
		}
		_markDirty();
	} else if (m_couponTimer)
	{
		m_couponTimer -= timeInterval;
		if (m_couponTimer <= 0.f)
		{
			m_couponTimer = 0;
			_generateCoupon();
		}
	}

	VN_LOG_DEBUG("UUID:"<<m_uuid<<"couponTimer:"<<m_couponTimer);
}

bool LoggedUser::CalculateSecondDayLogin(vn::u64 lastLoginTime)
{
	time_t curTime = time(NULL);
	time_t lastTime = lastLoginTime;
	tm *pLast = gmtime(&lastTime);
	tm *pCur = gmtime(&curTime);
	//获取年
	if (pCur->tm_year != pLast->tm_year)
	{
		return true;
	}
	//获取月
	if (pCur->tm_mon != pLast->tm_mon)
	{
		return true;
	}
	//获取日
	if (pCur->tm_mday != pLast->tm_mday)
	{
		return true;
	}
	return false;
}

void LoggedUser::CalculateLottery(vn::u64 lastCouponTime, vn::u64 lastLoginTime)
{
	vn::u64 curTime = time(NULL);
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"lastLotteryTimestamp is "<<lastCouponTime);
	vn::u64 timeInterval = curTime - lastCouponTime;
	//计算是否是第二天登录
	bool ifSecondDay = CalculateSecondDayLogin(lastLoginTime);
	/*if (m_lotteryState != kLottery_Opened)
	 {
	 return ;
	 }
	 m_lotteryTimer -= timeInterval;
	 if (m_lotteryTimer <= 0.f)
	 {
	 m_lotteryTimer = 0;
	 m_lotteryState = kLottery_Closed;
	 m_lotteryPool.clear();
	 }
	 VN_LOG_DEBUG("UUID:"<<m_uuid<<"lotteryState"<<m_lotteryState<<"lotteryTimer"<<m_lotteryTimer);*/

	//如果是第二天登录
	//重置Lottery
	if (ifSecondDay)
	{
		VN_LOG_DEBUG("uuid:"<<m_uuid<<"SecondDayLogin");
		if (m_lotteryState == LotteryState::kLottery_Locked)
		{
			return;
		}
		m_lotteryPool = BSAwardManager::instance().generate();
		m_lotteryState = kLottery_Opened;
		m_lotteryTimer = sWorldManager.instance().lottery_validTime();
	}
	else
	{
		if (m_lotteryState != kLottery_Opened)
		{
			return;
		}
		m_lotteryTimer -= timeInterval;
		if (m_lotteryTimer <= 0.f)
		{
			m_lotteryTimer = 0;
			m_lotteryState = kLottery_Closed;
			m_lotteryPool.clear();
		}
	}
}

void LoggedUser::GetLotteryPool(std::string &strLottery)
{
	strLottery = "";
	int size = m_lotteryPool.size();
	for (int i = 0; i < size; ++i)
	{
		std::string strId;
		ConvertU32ToStr(m_lotteryPool[i].first->awardId, strId);
		strLottery += strId + ",";
		std::string strNum;
		ConvertU32ToStr(m_lotteryPool[i].second, strNum);
		strLottery += strNum;
		if (i != size - 1)
		{
			strLottery += "|";
		}
	}
}

void LoggedUser::update(f32 deltaTime)
{
	//开始事务
	/*m_bsGoldMine.update(deltaTime);
	 m_bsIntrusion.update(deltaTime);
	 _updateCoupon(deltaTime);
	 _updateLottery(deltaTime);*/
	m_bsGoldMine.update(deltaTime);
	m_bsIntrusion.Asyncupdate(deltaTime, [this](bool ret)
	{
		if(!ret)
		{
			VN_LOG_ERROR("UUID:"<<GetUUId()<<"update intruion failed");
		}
	});
	//todo每隔一段时间跟新
	_updateCoupon(deltaTime);
	//todo
	_updateLottery(deltaTime);

	m_timer += deltaTime;
	if (m_timer >= 120.f)
	{
		m_timer = 0;
		if (m_dirty)
		{
			m_dirty = false;
			DataCenter::instance().saveUser( { this, true });
		}
	}
}

void LoggedUser::shutdown()
{
	if (m_dirty)
	{
		m_dirty = false;
		DataCenter::instance().saveUser( { this, true });
	}

}

void LoggedUser::_buildPlayerData(pkt::Msg_PlayerData &data)
{
	data.stamp = m_stamp;
	data.baseInfo.money = m_bsPlayerBaseInfo.money();
	data.baseInfo.diamond = m_bsPlayerBaseInfo.diamond();
	data.baseInfo.chip1 = m_bsPlayerBaseInfo.chip(0);
	data.baseInfo.chip2 = m_bsPlayerBaseInfo.chip(1);
	data.baseInfo.chip3 = m_bsPlayerBaseInfo.chip(2);
	data.techList = m_bsTechUpgrade.techList();
	const std::vector<std::vector<u32>> & towerList =
			m_bsTowerUpgrade.towerList();
	data.towerList.reserve(9);
	for (uint i = 0; i < towerList.size(); ++i)
	{
		for (uint j = 0; j < towerList[i].size(); ++j)
		{
			pkt::Msg_Tower tower;
			tower.cls = i;
			tower.idx = j;
			tower.count = towerList[i][j];
			data.towerList.push_back(tower);
		}
	}

	const std::map<u32, BSLevel::LevelInfo *> & levelList =
			m_bsLevel.levelList();
	for (auto &i : levelList)
	{
		pkt::Msg_LevelData li;
		li.id = i.second->level->f_LevelId.value;
		li.starNum = i.second->starNum;
		li.score = i.second->score;
		li.shovel = i.second->shovel;
		li.watch = i.second->watch;
		li.intrusion = i.second->intrusion;
		li.intrusionTimes = i.second->intrusionTimes;
		data.levelList.push_back(li);
	}

	const u32 *skull = m_bsHero.skull();
	const u32 *shovel = m_bsHero.shovel();
	const u32 *watch = m_bsHero.watch();

	data.heroRes.push_back(skull[0]);
	data.heroRes.push_back(skull[1]);
	data.heroRes.push_back(shovel[0]);
	data.heroRes.push_back(shovel[1]);
	data.heroRes.push_back(watch[0]);
	data.heroRes.push_back(watch[1]);
	const std::vector<BSHero::HeroInfo> &heroList = m_bsHero.heroList();
	for (auto &i : heroList)
	{
		pkt::Msg_Hero hero;
		hero.id = i.id;
		hero.towerClass = i.towerClass;
		hero.unlockRes = i.unlockRes;
		hero.status = (u32) i.status;

		for (auto &j : i.runeList)
		{
			pkt::Msg_Rune rune;
			rune.id = j.id;
			rune.status = (u32) j.status;
			hero.runeList.push_back(rune);
		}
		data.heroList.push_back(hero);
	}

	const std::map<u32, u32> &itemMap = m_bsItemStore.itemMap();
	for (auto &i : itemMap)
	{
		pkt::Msg_Item item;
		item.id = i.first;
		item.count = i.second;
		data.itemList.push_back(item);
	}

	const std::map<u32, bool> &monsterMap = m_bsMonsterMap.monsterMap();
	for (auto &i : monsterMap)
	{
		pkt::Msg_Monster monster;
		monster.id = i.first;
		monster.isMeet = i.second;
		data.monsterList.push_back(monster);
	}

	auto &disableList = m_bsForceGuide.disableList();
	for (auto i : disableList)
	{
		data.disableList.push_back(i);
	}

	auto &platformList = m_bsPlatformStore.list();
	data.platformList.assign(platformList.begin(), platformList.end());

	auto &groups = m_bsLevelGroupMgr.groups();
	data.groupList.reserve(groups.size());
	for (auto &i : groups)
	{
		data.groupList.push_back(pkt::Msg_GroupData());
		auto &grp = data.groupList.back();
		grp.groupId = i.first;
		grp.passed = i.second.passed;
		grp.rewards = (u32) i.second.rewardsCount;
		grp.intrusionTimes = i.second.intrusionTimes;
		grp.levels.reserve(i.second.levels.size());
		for (auto &j : i.second.levels)
		{
			pkt::Msg_GroupLevelData t;
			t.id = j.first;
			t.passed = j.second.passed;
			t.stars = j.second.stars;
			t.watch = j.second.watch;
			t.shovel = j.second.shovel;
			t.score = 0;
			t.resolved = j.second.resolved;
			t.heroes = j.second.heros;
			t.towers.reserve(j.second.towers.size());
			for (auto &k : j.second.towers)
			{
				t.towers.push_back( { k.first, k.second });
			}
			t.platforms.reserve(j.second.platforms.size());
			for (auto &k : j.second.platforms)
			{
				t.platforms.push_back( { k.first, k.second });
			}
			grp.levels.push_back(t);
		}
	}

	//mission
	auto &mission = m_bsMission.list();
	data.missionRewardList.reserve(mission.size());
	for (auto &i : mission)
	{
		data.missionRewardList.push_back(pkt::Msg_MissionRewardInfo());
		auto &mis = data.missionRewardList.back();
		mis.id = i.first;
		mis.get = i.second;
	}

	_buildTimedData(data.timedData);
}

void LoggedUser::_buildTimedData(pkt::Msg_TimedData &data)
{
	auto &mines = m_bsGoldMine.mines();
	data.goldMineList.reserve(mines.size());
	for (auto &i : mines)
	{
		pkt::Msg_GoldMine gm;
		gm.id = i.first;
		gm.nowNum = i.second.nowNum;
		data.goldMineList.push_back(gm);
	}
}
void LoggedUser::_sendAnnouncementId()	//下发通告id
{
	if (!m_client)
	{
		return;
	}
	pkt::res_announcementId pkt;

	// App::instance().announcement(pkt.data);
	sWorldManager.announcement(pkt.data.data,NULL);
	m_client->sendPacket(pkt);

}

void LoggedUser::_sendCouponInfo()	//下发优惠券消息
{
	if (!m_client)
	{
		return;
	}
	if (!m_coupon)
	{
		pkt::res_couponNone res;
		m_client->sendPacket(res);
		return;
	}
	struct _
	{
		static void _fillCouponBaseInfo(const BSCoupon *coupon,
				pkt::Msg_CouponBaseInfo &info)
		{
			info.imageId = coupon->imageId;
			info.subImageId = coupon->subImageId;
			info.title = coupon->title;
			info.desc = coupon->desc;
			info.cost = coupon->cost;
			info.originalCost = coupon->originalCost;
		}
	};

	switch (m_coupon->type())
	{
	case BSCoupon::kCT_Hero:
	{
		pkt::res_couponHero pkt;
		_::_fillCouponBaseInfo(m_coupon, pkt.data.data.info);
		pkt.data.data.heroClassId =
				static_cast<const BSCoupon_Hero *>(m_coupon)->heroClassId;
		pkt.data.data.info.validTime = m_couponTimer;
		m_client->sendPacket(pkt);
		break;
	}

	case BSCoupon::kCT_Items:
	{
		pkt::res_couponItems pkt;
		_::_fillCouponBaseInfo(m_coupon, pkt.data.data.info);
		auto pc = static_cast<const BSCoupon_Items *>(m_coupon);
		pkt.data.data.items.reserve(pc->items.size());
		for (auto &i : pc->items)
		{
			pkt::Msg_Item item = { i.first, i.second };
			pkt.data.data.items.push_back(item);
		}
		pkt.data.data.info.validTime = m_couponTimer;
		m_client->sendPacket(pkt);
		break;
	}

	case BSCoupon::kCT_Diamonds:
	{
		pkt::res_couponDiamonds pkt;
		_::_fillCouponBaseInfo(m_coupon, pkt.data.data.info);
		u32 pid = static_cast<const BSCoupon_Diamonds *>(m_coupon)->productId;
		auto product = BSRmbShopManager::instance().find_product(pid);
		if (product)
		{
			pkt.data.data.diamonds = product->count;
			pkt.data.data.productId = pid;
			pkt.data.data.info.validTime = m_couponTimer;
			m_client->sendPacket(pkt);
		}
		break;
	}
	}
}
void LoggedUser::_sendLotteryInfo() //下发彩票
{
	if (!m_client)
	{
		return;
	}
	if (m_lotteryState != kLottery_Opened)
	{
		pkt::res_lotteryNone res;
		m_client->sendPacket(res);
		return;
	}

	pkt::res_lotteryInfo pkt;
	pkt.data.data.validTime = m_lotteryTimer;
	pkt.data.data.drawTimes = m_lotteryDrawTimes;
	pkt.data.data.drawCost = sWorldManager.instance().lottery_drawCost();
	pkt.data.data.awards.reserve(m_lotteryPool.size());
	for (auto &i : m_lotteryPool)
	{
		auto p = i.first;
		pkt.data.data.awards.push_back( { });
		pkt::Msg_Award &award = pkt.data.data.awards.back();
		award.rank = p->rank;
		award.desc = p->desc;
		award.imageId = p->imageId;
		award.money = p->money;
		award.diamonds = p->diamonds;
		award.items.reserve(p->items.size());
		for (auto &i : p->items)
		{
			award.items.push_back( { i.first, i.second });
		}
	}
	m_client->sendPacket(pkt);
}
void LoggedUser::_sendRegistrationInfo()
{
	if (!m_client)
	{
		return;
	}
	pkt::res_updataRegistration pkt;
	auto today = DateTime::today();
	++today.month;
	if (m_bsRegistration.m_firstDate.year == 0)
	{
		pkt.data.data.complete = false;
		pkt.data.data.leftDay = 6;
		pkt.data.data.nums = 0;
		m_bsRegistration.m_group = 0;
		pkt.data.data.groups = m_bsRegistration.m_group;
	} else if (BSRegistrationManager::instance().daysBetween2Date(
			m_bsRegistration.m_firstDate, today) > 6)
	{
		pkt.data.data.complete = false;
		pkt.data.data.leftDay = 6;
		pkt.data.data.nums = 0;
		++m_bsRegistration.m_group;
		m_bsRegistration.m_group %= 4;
		pkt.data.data.groups = m_bsRegistration.m_group;
	} else
	{
		pkt.data.data.complete = (m_bsRegistration.m_lastDate == today);
		pkt.data.data.leftDay = (6
				- BSRegistrationManager::instance().daysBetween2Date(
						m_bsRegistration.m_firstDate, today));
		pkt.data.data.nums = m_bsRegistration.m_nums;
		pkt.data.data.groups = m_bsRegistration.m_group;
	}
	m_client->sendPacket(pkt);
}

void LoggedUser::save(Variable_object &vobj) const
{
	vobj.setAttribute("stamp", vnnew Variable_int32(m_stamp));
	vobj.setAttribute("purchased_diamonds",
			vnnew Variable_int32(m_purchasedDiamonds));
	vobj.setAttribute("coupon_id",
			vnnew Variable_int32((u32) (m_coupon ? m_coupon->couponId : 0)));
	vobj.setAttribute("coupon_timer", vnnew Variable_float(m_couponTimer));
	vobj.setAttribute("lottery_state",
			vnnew Variable_int32((u32) m_lotteryState));
	vobj.setAttribute("lottery_draw", vnnew Variable_int32(m_lotteryDrawTimes));
	vobj.setAttribute("lottery_timer", vnnew Variable_float(m_lotteryTimer));

	auto lottery_pool = vnnew Variable_object();
	for (auto &i : m_lotteryPool)
	{
		lottery_pool->add(vnnew Variable_int32(i.first->awardId));
		lottery_pool->add(vnnew Variable_int32(i.second));
	}
	vobj.setAttribute("lottery_pool", lottery_pool);

	m_bsPlayerBaseInfo.save(vobj);
	m_bsTechUpgrade.save(vobj);
	m_bsTowerUpgrade.save(vobj);
	m_bsLevel.save(vobj);
	m_bsGoldMine.save(vobj);
	m_bsHero.save(vobj);
	m_bsIntrusion.save(vobj);
	m_bsItemStore.save(vobj);
	m_bsMonsterMap.save(vobj);
	m_bsForceGuide.save(vobj);
	m_bsPlatformStore.save(vobj);
	m_bsLevelGroupMgr.save(vobj);
	m_bsMission.save(vobj);
	m_bsRegistration.save(vobj);

	auto arr = vnnew Variable_object();
	vobj.setAttribute("redeem_categories", arr);
	for (auto i : m_redeemPacketCategories)
	{
		arr->add(vnnew Variable_int32(i));
	}
}

void LoggedUser::load(PreparedQueryResult baseinfo, vn::s32 balance)
{
	//todo lotterypool
	m_bsPlayerBaseInfo.load(baseinfo, balance);
	//u64 nowTime =  time(NULL);
	m_purchasedDiamonds = (*baseinfo)[5].GetUInt32();
	m_stamp = (*baseinfo)[6].GetUInt32();
	u32 couponId = (*baseinfo)[7].GetUInt32();
	if (couponId)
	{
		m_coupon = BSCouponManager::instance().find(couponId);
	} else
	{
		m_coupon = 0;
	}
	if (m_coupon)
	{
		m_couponTimer = (*baseinfo)[8].GetFloat();
	} else
	{
		m_couponTimer = 0;
	}
	m_lastCouponTimeStamp = (*baseinfo)[9].GetUInt64();
	m_bsHero.load(baseinfo);
	m_bsIntrusion.load(baseinfo);
	m_bsRegistration.load(baseinfo);
	m_lotteryDrawTimes = (*baseinfo)[23].GetUInt32();
	m_lotteryState = (LotteryState) (*baseinfo)[24].GetInt32();
	m_lotteryTimer = (*baseinfo)[25].GetFloat();
	//lotterypool
	std::string strLotteryPool = (*baseinfo)[26].GetString();
	//
	if (strLotteryPool != "")
	{
		_ConvertStrToLotteryPool(strLotteryPool);
	}
	m_lastLotteryTimeStamp = (*baseinfo)[27].GetUInt64();
	m_lastLoginTimeStamp = (*baseinfo)[28].GetUInt64();
}

//1,2|2,3    => <1,2> <2,3>
void LoggedUser::_ConvertStrToLotteryPool(std::string &strPool)
{
	if (strPool == "")
	{
		return;
	}
	std::vector<std::string> desPool;
	StrSplit(strPool, "|", desPool);
	for (int i = 0; i < (s32) desPool.size(); ++i)
	{
		std::vector<std::string> pairCon;
		StrSplit(desPool[i], ",", pairCon);
		const BSAward *pAward = BSAwardManager::instance().find(
				ConvertStrToU32(pairCon[0]));
		if (!pAward)
		{
			m_lotteryPool = BSAwardManager::instance().generate();
			break;
		}
		m_lotteryPool.push_back( { pAward, ConvertStrToU32(pairCon[1]) });
	}

}

void LoggedUser::load(Variable_object &vobj)
{
	m_stamp = vobj.queryAttributeInt32("stamp");
	m_purchasedDiamonds = vobj.queryAttributeInt32("purchased_diamonds");
	u32 couponId = vobj.queryAttributeInt32("coupon_id");
	if (couponId)
	{
		m_coupon = BSCouponManager::instance().find(couponId);
	} else
	{
		m_coupon = 0;
	}
	if (m_coupon)
	{
		m_couponTimer = vobj.queryAttributeFloat("coupon_timer");
	} else
	{
		m_couponTimer = 0;
	}

	m_lotteryState = (LotteryState) vobj.queryAttributeInt32("lottery_state");
	m_lotteryDrawTimes = vobj.queryAttributeInt32("lottery_draw");
	m_lotteryTimer = vobj.queryAttributeFloat("lottery_timer");

	VariableAccessor<Variable_int32, Variable_int32> lottery_pool(
			vobj.queryAttributeObject("lottery_pool"));
	while (lottery_pool.fetch())
	{
		auto p = BSAwardManager::instance().find(lottery_pool.at<0>()->value());
		if (!p)
		{
			m_lotteryPool = BSAwardManager::instance().generate();
			break;
		}
		m_lotteryPool.push_back( { p, (u32) lottery_pool.at<1>()->value() });
	}

	m_bsPlayerBaseInfo.load(vobj);
	m_bsTechUpgrade.load(vobj);
	//每3个一类
	m_bsTowerUpgrade.load(vobj);
	m_bsLevel.load(vobj);
	m_bsGoldMine.load(vobj);
	m_bsHero.load(vobj);
	m_bsIntrusion.load(vobj);
	m_bsItemStore.load(vobj);
	m_bsMonsterMap.load(vobj);
	m_bsForceGuide.load(vobj);
	m_bsPlatformStore.load(vobj);
	m_bsLevelGroupMgr.load(vobj);
	m_bsMission.load(vobj);
	m_bsRegistration.load(vobj);

	VariableAccessor<Variable_int32> redeem_categories(
			vobj.queryAttributeObject("redeem_categories"));
	m_redeemPacketCategories.clear();
	while (redeem_categories.fetch())
	{
		m_redeemPacketCategories.insert(redeem_categories.at<0>()->value());
	}

	m_bsGoldMine._calcYield();
}

BSPlayerBaseInfo & LoggedUser::bsPlayerBaseInfo()
{
	return m_bsPlayerBaseInfo;
}

BSTechUpgrade & LoggedUser::bsTechUpgrade()
{
	return m_bsTechUpgrade;
}

BSTowerUpgrade & LoggedUser::bsTowerUpgrade()
{
	return m_bsTowerUpgrade;
}

BSPlatformStore & LoggedUser::bsPlatformStore()
{
	return m_bsPlatformStore;
}

BSMonsterMap & LoggedUser::bsMonsterMap()
{
	return m_bsMonsterMap;
}
BSLevel & LoggedUser::bsLevel()
{
	return m_bsLevel;
}

BSGoldMine & LoggedUser::bsGoldMine()
{
	return m_bsGoldMine;
}

BSHero & LoggedUser::bsHero()
{
	return m_bsHero;
}

BSLevelGroupManager & LoggedUser::bsLevelGroupMgr()
{
	return m_bsLevelGroupMgr;
}

BSMission & LoggedUser::bsMission()
{
	return m_bsMission;
}

BSRegistration & LoggedUser::bsRegistration()
{
	return m_bsRegistration;
}

BSItemStore & LoggedUser::bsItemStore()
{
	return m_bsItemStore;
}

BSForceGuide & LoggedUser::bsForceGuide()
{
	return m_bsForceGuide;
}

void LoggedUser::DbOperFailHandl()
{
	if (m_client)
	{
		pkt::res_invalid_operation msg;
		m_client->sendPacket(msg, Session::kClose);
	}
}

void LoggedUser::_invalid_operation()
{
	if (m_client)
	{
		pkt::res_invalid_operation msg;
		m_client->sendPacket(msg, Session::kClose);
	}
}

void LoggedUser::reset_data(vn::s32 diamond)
{
	m_stamp = 0;
	m_bsPlayerBaseInfo.init(diamond);
	m_bsTechUpgrade.init();
	m_bsTowerUpgrade.init();
	m_bsLevel.init();
	m_bsGoldMine.init();
	m_bsHero.init();
	m_bsIntrusion.init();
	m_bsItemStore.init();
	m_bsMonsterMap.init();
	m_bsForceGuide.init();
	m_bsPlatformStore.reset();
	m_bsLevelGroupMgr.clear();
	m_bsMission.reset();
	m_bsRegistration.reset();
	m_coupon = 0;
	m_couponTimer = 0;
	m_lotteryState = kLottery_Locked;
	m_lotteryTimer = 0;
	m_lotteryPool.clear();
	m_lotteryDrawTimes = 5;
}

void LoggedUser::order_ok(const str8 &cooOrderSerial, s32 *addDiamond)
{
	/*  UUID uuid;
	 u32 productId;
	 auto status = DataCenter::instance().queryOrder(cooOrderSerial, uuid, productId);
	 //找到订单
	 if (status == DataCenter::kOrderStatus_Paying && uuid == m_uuid)
	 {
	 DataCenter::instance().finishOrder(cooOrderSerial, m_uuid, true);

	 const BSRmbShopManager::Product *product = BSRmbShopManager::instance().find_product(productId);
	 if (product)
	 {
	 _updateStamp();
	 m_bsPlayerBaseInfo.addDiamond(product->count);
	 if(addDiamond)
	 {
	 *addDiamond =product->count;
	 }
	 pkt::res_productContent pkt;
	 pkt.data.data.diamond = product->count;
	 if (m_client)
	 {
	 m_client->sendPacket(pkt);
	 }
	 }
	 if (m_coupon && m_coupon->type() == BSCoupon::kCT_Diamonds)
	 {
	 if (static_cast<const BSCoupon_Diamonds *>(m_coupon)->productId == productId) {
	 _notify_couponUsed();
	 }
	 }
	 }
	 pkt::res_orderDone res;
	 res.data.data = cooOrderSerial;
	 if (m_client) {
	 m_client->sendPacket(res);
	 }*/
}

void LoggedUser::Asyncorder_ok(const str8 &cooOrderSerial,
		std::function<void(bool ret)> doResult)
{
	UUID uuid;
	u32 productId;
	auto status = DataCenter::instance().queryOrder(cooOrderSerial, uuid,
			productId);
	//找到订单
	if (status == DataCenter::kOrderStatus_Paying && uuid == m_uuid)
	{
		DataCenter::instance().finishOrder(cooOrderSerial, m_uuid, true);
		const BSRmbShopManager::Product *product =
				BSRmbShopManager::instance().find_product(productId);
		if (m_coupon && m_coupon->type() == BSCoupon::kCT_Diamonds)
		{
			if (static_cast<const BSCoupon_Diamonds *>(m_coupon)->productId
					== productId)
			{
				_notify_couponUsed();
			}
		}
		bool ifProduct = false;
		u32 productCount = 0;
		//找到商品
		if (product)
		{
			ifProduct = true;
			_updateStamp();
			m_bsPlayerBaseInfo.addDiamond(product->count);
			//pkt::res_productContent pkt;
			//pkt.data.data.diamond = product->count;
			productCount = product->count;
			/*if (m_client)
			 {
			 m_client->sendPacket(pkt);
			 }*/
		}
		//更新db
		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
				this, true }, { this, true },
				[this,ifProduct,doResult,productCount](bool result)
				{
					if(!result)
					{
						return doResult(false);
					}
					if(ifProduct)
					{
						pkt::res_productContent pkt;
						pkt.data.data.diamond = productCount;
						if (m_client)
						{
							m_client->sendPacket(pkt);
						}
						return doResult(true);
					}
				});
	} else
	{
		return doResult(false);
	}
	pkt::res_orderDone res;
	res.data.data = cooOrderSerial;
	if (m_client)
	{
		m_client->sendPacket(res);
	}
}

void LoggedUser::order_failed(const str8 &cooOrderSerial)
{
	DataCenter::instance().finishOrder(cooOrderSerial, m_uuid, false);

	pkt::res_orderDone res;
	res.data.data = cooOrderSerial;
	if (m_client)
	{
		m_client->sendPacket(res);
	}
}

/////////////////////////////////////////////////逻辑
//todo
VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_reset)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_reset");
	return;
	reset_data();
	pkt::res_playerData res;
	_buildPlayerData(res.data.data);
	m_client->sendPacket(res);
	_sendAnnouncementId();
	_sendRegistrationInfo();
}

//登录后请求玩家数据
VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_playerData)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_playerData");
	pkt::res_playerData res;
	//发送timestamp 到客户端存起来
	_buildPlayerData(res.data.data);
	m_client->sendPacket(res);
	_sendAnnouncementId();
	_sendCouponInfo();
	_sendLotteryInfo();
	_sendRegistrationInfo();
}
//请求通告
VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_announcement)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_announcement");
	if (m_client)
	{
		pkt::res_announcement res;
		sWorldManager.instance().announcement(res.data.data.id, &res.data.data.content);
		m_client->sendPacket(res);
	}
}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_timedData)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_timedData");
	pkt::res_timedData res;
	_buildTimedData(res.data.timeData);
	m_client->sendPacket(res);
	_sendAnnouncementId();
	_sendCouponInfo();
	_sendLotteryInfo();
	_sendRegistrationInfo();
}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt, req_techUpgrade)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_techUpgrade");
	vn::s32 diamondPay = 0;
	if (m_bsTechUpgrade.upgrade_tech(pkt->data.data, &diamondPay))
	{
		_updateStamp();
		//更新techupgrade表
		//开启任务
		u32 techIndex = pkt->data.data;
		AsyncTaskPtr taskPtr = vnnew AsyncTask();
		taskPtr->Init(2);
		taskPtr->m_diamondCount = diamondPay;
		m_bsTechUpgrade.Asyncsave(techIndex,
				[this,taskPtr,techIndex,diamondPay](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"techindex"<<techIndex<<"techUpdate failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
		//
		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
				this, true }, { this, true },
				[this,taskPtr,diamondPay](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
	} else
	{
		_invalid_operation();
	}
}

void LoggedUser::_HandlClose(AsyncTaskPtr task)
{
	if (!task->IsFinished())
	{
		return;
	}
	bool ret = true;
	for (int i = 0; i < task->TaskSize(); ++i)
	{
		if (!task->at(i))
		{
			ret = false;
			break;
		}
	}
	if (!ret)
	{
		DbOperFailHandl();
		return;
	}
	LoggedUserManager::instance().removeUser(m_uuid);
	queue()->detach( { this, true });
	//记录日志
	//DataCenter::instance().log_user_logout(m_uuid);
}

//DealsvrHandler.cpp:
void LoggedUser::_HandlTaskFinish(AsyncTaskPtr task)
{
	if (!task->IsFinished())
	{
		return;
	}
	bool ret = true;
	for (int i = 0; i < task->TaskSize(); ++i)
	{
		if (!task->at(i))
		{
			ret = false;
			break;
		}
	}
	if (!ret)
	{
		DbOperFailHandl();
		return;
	}
	std::string strUUId = this->GetUUId();
	s32 diamonds = task->m_diamondCount;
	if (diamonds > 0)
	{
		PPLinkManger::CallbackFn callback =
				[this,strUUId,diamonds](vn::s32 result,std::string& context)
				{
					VN_LOG_DEBUG("strUUId:"<<strUUId<<"Add diamond:"<<diamonds);
					int nRet=0;
					if(!ParseResult(&nRet,context))
					{
						VN_LOG_ERROR("PARSE ERROR");
						return;
					}
					if(nRet<0)
					{
						VN_LOG_ERROR("Present Diamond ERROR");
						return;
					}
					//获取余额
					PPLinkManger::CallbackFn getBalCallFun =[this](vn::s32 result,std::string& context)
					{
						int nRet=0;
						if(!ParseResult(&nRet,context))
						{
							VN_LOG_ERROR("PARSE ERROR");
							return;
						}
						if(nRet<0)
						{
							VN_LOG_ERROR("Present Diamond ERROR");
							return;
						}
						s32 nBalance = 0;
						if(!ParseDiamond(&nBalance,context))
						{
							VN_LOG_ERROR("PARSE ERROR 	ERROR");
							return;
						}
						m_bsPlayerBaseInfo.SetDiamond(nBalance);
					};
					//查询余额
					sPPLinkMgr.SendGetBalance(
							{	this,true},std::move(getBalCallFun),
							(char*)strUUId.c_str());

				};
		//发送req_Present添加钻石
		sPPLinkMgr.SendPresent(
				{	this,true},std::move(callback),
				(char*)strUUId.c_str(),diamonds);
	}
	else if(diamonds < 0)
	{
		//发送req_Pay减少钻石
		PPLinkManger::CallbackFn callback = [this,strUUId,diamonds](vn::s32 result,std::string& context)
		{
			VN_LOG_DEBUG("strUUId:"<<strUUId<<"Reduce diamond:"<<diamonds);
			//获取余额
			//解析context
			s32 nResultCode=0;
			vn::Variable_int32 type;
			std::string strKey = "ret";
			if(!ParseJson(&nResultCode,vn::kVT_int32,type,context,strKey))
			{
				VN_LOG_ERROR("Parase FAILED");
				return;
			}
			VN_LOG_DEBUG("nResultCode:"<<nResultCode);
			if( nResultCode <0 )
			{
				VN_LOG_ERROR("Pay FAILED");
				return;
			}
			//获取金币
			s32 nBalance=0;
			//vn::Variable_int32 type;
			strKey = "balance";
			if(!ParseJson(&nBalance,vn::kVT_int32,type,context,strKey))
			{
				VN_LOG_ERROR("Parase FAILED");
				return;
			}
			VN_LOG_DEBUG("nBalance:"<<nBalance);
			m_bsPlayerBaseInfo.SetDiamond(nBalance);
		};
		//发送req_Present添加钻石
		sPPLinkMgr.SendPay(
				{	this,true},std::move(callback),
				(char*)strUUId.c_str(),-diamonds);
	}
}

VN_IMPL_PACKET_REQUEST2(LoggedUser, pkt,req_towerUpgrade)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_towerUpgrade");
	u32 towerclassIndex = pkt->data.data.cls;
	u32 towerinClassIndex = pkt->data.data.idx;
	s32 diamondPay = 0;
	if (m_bsTowerUpgrade.upgrade_tower(pkt->data.data.cls, pkt->data.data.idx,
			&diamondPay))
	{
		_updateStamp();
		AsyncTaskPtr taskPtr = vnnew AsyncTask();
		taskPtr->Init(2);
		taskPtr->m_diamondCount = diamondPay;
		m_bsTowerUpgrade.Asyncsave(towerclassIndex, towerinClassIndex,
				[this,towerclassIndex,towerinClassIndex,taskPtr,diamondPay](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"towerclassIndex"<<towerclassIndex<<"towerinClassIndex"<<towerinClassIndex<<"techUpdate failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
		//
		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
				this, true }, { this, true },
				[this,taskPtr,diamondPay](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
	} else
	{
		_invalid_operation();
	}
}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt, req_beginBattle)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_beginBattle");
	if (pkt->data.data.stamp != m_stamp)
	{
		//客户端清除timestamp
		_invalid_operation();
	} else
	{
		// TODO: check levelId!!!
		auto *level = BSFactory::instance().find_level(pkt->data.data.levelId);
		if (!level)
		{
			_invalid_operation();
		} else
		{
			m_battle = true;
			pkt::res_beginBattle res;
			res.data.data = pkt->data.data.levelId;
			if (m_client)
			{
				m_client->sendPacket(res);
			}
			bool t;
			if (level->f_LevelGroupId.value)
			{
				t = m_bsLevelGroupMgr.intrusion(level->f_LevelGroupId.value);
			} else
			{
				t = m_bsLevel.intrusion(pkt->data.data.levelId);
			}
			//DataCenter::instance().log_user_beginBattle(m_uuid, pkt->data.data.levelId, t);
			auto log = vnnew DC_DataBase_EventLog_User_BeginBattle;
			log->strUUId = GetUUId();
			log->levelId = pkt->data.data.levelId;
			log->intrusion = t;
			UserEventLogPtr logPtr(log);
			logPtr->Asyncsave( { this, true }, [](bool ret)
			{
				if(!ret)
				{
					VN_LOG_ERROR("BeginBattle save failed");
				}
			});
		}
	}
}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt, req_battleVictory)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_battleVictory");
	u32 levelId = pkt->data.data.id;
	auto *level = BSFactory::instance().find_level(pkt->data.data.id);
	if (!level || level->f_LevelGroupId.value || !m_battle)
	{
		_invalid_operation();
	} else
	{
		m_battle = false;
		bool ifNewLevel;
		s32 addDiamond = 0;
		if (m_bsLevel.update_levelInfo(level, pkt->data.data.stars,
				pkt->data.data.score, pkt->data.data.shovel,
				pkt->data.data.watch, &m_battleIntrusion, &ifNewLevel,
				&addDiamond))
		{
			_updateStamp();
			//m_bsHero.open_hero();
			if (!m_bsHero.SyncOpenHero())
			{
				DbOperFailHandl();
				return;
			}
			_checkLottery(pkt->data.data.id);
			if (pkt->data.data.stars == 3)
			{
				m_bsIntrusion.intrusion();
			}
			if (_generateCoupon())
			{
				_sendCouponInfo();
			}
			m_battleId = pkt->data.data.id;
			AsyncTaskPtr taskPtr = vnnew AsyncTask();
			taskPtr->Init(3);
			taskPtr->m_diamondCount = addDiamond;
			if (ifNewLevel)
			{
				m_bsLevel.AsyncInsert(levelId,
						[this,taskPtr,levelId,addDiamond](bool ret)
						{
							if(!ret)
							{
								VN_LOG_ERROR("UUId:"<<GetUUId()<<"levelid"<<levelId<<"update level failed");
							}
							taskPtr->FinishTask(ret);
							_HandlTaskFinish(taskPtr);
						});

			} else
			{
				m_bsLevel.AsyncUpdate(levelId,
						[this,taskPtr,levelId](bool ret)
						{
							if(!ret)
							{
								VN_LOG_ERROR("UUId:"<<GetUUId()<<"levelid"<<levelId<<"update level failed");
							}
							taskPtr->FinishTask(ret);
							_HandlTaskFinish(taskPtr);
						});
			}

			((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
					this, true }, { this, true },
					[this,taskPtr](bool ret)
					{
						if(!ret)
						{
							VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
						}
						taskPtr->FinishTask(ret);
						_HandlTaskFinish(taskPtr);
					});
			//DataCenter::instance().log_user_endBattle(m_uuid, pkt->data.data.id, m_battleIntrusion, pkt->data.data.stars, pkt->data.data.watch, pkt->data.data.shovel);
			auto log = vnnew DC_DataBase_EventLog_User_EndBattle;
			log->strUUId = GetUUId();
			log->levelId = pkt->data.data.id;
			log->intrusion = m_battleIntrusion;
			log->stars = pkt->data.data.stars;
			log->watch = pkt->data.data.watch;
			log->shovel = pkt->data.data.shovel;
			UserEventLogPtr logPtr(log);
			logPtr->Asyncsave( { this, true }, [this,taskPtr](bool ret)
			{
				if(!ret)
				{
					VN_LOG_ERROR("ENDBattle save failed");
				}
				taskPtr->FinishTask(ret);
				_HandlTaskFinish(taskPtr);
			});
		} else
		{
			_invalid_operation();
		}
	}
}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt, req_battleFailed)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_battleFailed");
	if (m_battle)
	{
		m_battle = false;
		_updateStamp();
		//DataCenter::instance().log_user_endBattle(m_uuid, m_battleId, m_battleIntrusion, 0, false, false);
		std::string strUUid = GetUUId();
		AsyncTaskPtr taskPtr = vnnew AsyncTask();
		taskPtr->Init(2);
		auto log = vnnew DC_DataBase_EventLog_User_EndBattle;
		log->strUUId = GetUUId();
		log->levelId = m_battleId;
		log->intrusion = m_battleIntrusion;
		log->stars = 0;
		log->watch = false;
		log->shovel = false;
		UserEventLogPtr logPtr(log);
		logPtr->Asyncsave( { this, true }, [this,taskPtr](bool ret)
		{
			if(!ret)
			{
				VN_LOG_ERROR("UUId:"<<GetUUId()<<"ENDBattle save failed");
			}
			taskPtr->FinishTask(ret);
			_HandlTaskFinish(taskPtr);
		});

		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateTimeStamp( { this,
				true }, strUUid, m_stamp, [this,taskPtr](bool ret)
		{
			if(!ret)
			{
				VN_LOG_ERROR("uuid"<<GetUUId()<<"update stamp failed");

			}
			taskPtr->FinishTask(ret);
			_HandlTaskFinish(taskPtr);
		});
	} else
	{
		_invalid_operation();
	}
}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt, req_resetGroupLevel)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_resetGroupLevel");
	u32 levelId = pkt->data.data;
	//todo
	if (m_bsLevelGroupMgr.reset(pkt->data.data))
	{
		//todo
		_updateStamp();
		AsyncTaskPtr taskPtr = vnnew AsyncTask();
		taskPtr->Init(2);
		m_bsLevelGroupMgr.Asyncsave(levelId, [this,taskPtr](int ret)
		{
			if(ret == -1)
			{
				_invalid_operation();
				return;
			}
			taskPtr->FinishTask(ret);
			_HandlTaskFinish(taskPtr);
		});
		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
				this, true }, { this, true },
				[this,taskPtr](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
	} else
	{
		_invalid_operation();
	}
}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_groupBattleVictory)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_groupBattleVictory");
	if (!m_battle)
	{
		_invalid_operation();
	} else
	{
		m_battle = false;
		std::vector<std::pair<u32, u32>> towers, platforms;
		towers.reserve(pkt->data.data.towers.size());
		for (auto &i : pkt->data.data.towers)
		{
			towers.push_back( { i.index, i.count });
		}
		platforms.reserve(pkt->data.data.platforms.size());
		for (auto &i : pkt->data.data.platforms)
		{
			platforms.push_back( { i.index, i.count });
		}
		bool ifNew = false;
		u32 levelId = pkt->data.data.data.id;
		s32 addDiamond = 0;
		if (!m_bsLevelGroupMgr.win(levelId, pkt->data.data.data.stars,
				pkt->data.data.data.watch, pkt->data.data.data.shovel,
				pkt->data.data.heroes, towers, platforms, &m_battleIntrusion,
				&ifNew, &addDiamond))
		{
			_invalid_operation();
			return;
		} else
		{
			_updateStamp();
			//m_bsHero.open_hero();
			if (!m_bsHero.SyncOpenHero())
			{
				DbOperFailHandl();
				return;
			}
			if (_generateCoupon())
			{
				_sendCouponInfo();
			}
			AsyncTaskPtr taskPtr = vnnew AsyncTask();
			taskPtr->Init(3);
			taskPtr->m_diamondCount = addDiamond;
			if (ifNew)
			{
				m_bsLevelGroupMgr.AsyncsaveNew(levelId, [this,taskPtr](int ret)
				{
					if(ret == -1)
					{
						_invalid_operation();
						return;
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
			} else
			{
				m_bsLevelGroupMgr.Asyncsave(levelId, [this,taskPtr](int ret)
				{
					if(ret == -1)
					{
						_invalid_operation();
						return;
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
			}
			((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
					this, true }, { this, true },
					[this,taskPtr](bool ret)
					{
						if(!ret)
						{
							VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
						}
						taskPtr->FinishTask(ret);
						_HandlTaskFinish(taskPtr);
					});
			//DataCenter::instance().log_user_endBattle(m_uuid, pkt->data.data.data.id, m_battleIntrusion, pkt->data.data.data.stars, pkt->data.data.data.watch, pkt->data.data.data.shovel);
			auto log = vnnew DC_DataBase_EventLog_User_EndBattle;
			log->strUUId = GetUUId();
			log->levelId = pkt->data.data.data.id;
			log->intrusion = m_battleIntrusion;
			log->stars = pkt->data.data.data.stars;
			log->watch = pkt->data.data.data.watch;
			log->shovel = pkt->data.data.data.shovel;
			UserEventLogPtr logPtr(log);
			logPtr->Asyncsave( { this, true }, [this,taskPtr](bool ret)
			{
				if(!ret)
				{
					VN_LOG_ERROR("UUId:"<<GetUUId()<<"ENDBattle save failed");
				}
				taskPtr->FinishTask(ret);
				_HandlTaskFinish(taskPtr);
			});
		}
	}
}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt, req_collectGoldMine)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_collectGoldMine");
	u32 mineId = pkt->data.data.id;
	s32 addDiamond = 0;
	if (m_bsGoldMine.collect(pkt->data.data.id, pkt->data.data.num,
			&addDiamond))
	{
		_updateStamp();
		AsyncTaskPtr taskPtr = vnnew AsyncTask();
		taskPtr->Init(2);
		taskPtr->m_diamondCount = addDiamond;
		bsGoldMine().AsyncSave( { this, true },
				[this,mineId,taskPtr](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("uuid"<<GetUUId()<<"mineid:"<<mineId<<"update failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
				this, true }, { this, true },
				[this,taskPtr](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
	} else
	{
		_invalid_operation();
	}
}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt, req_upgradeHero)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_upgradeHero");
	u32 heroId = pkt->data.data;
	s32 needDiamond = 0;
	if (m_bsHero.upgrade_hero(heroId, false, &needDiamond))
	{
		_updateStamp();
		AsyncTaskPtr taskPtr = vnnew AsyncTask();
		taskPtr->Init(2);
		taskPtr->m_diamondCount = needDiamond;
		m_bsHero.Asyncsave(heroId,
				[this,taskPtr,heroId](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"heroId"<<heroId<<"update failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
				this, true }, { this, true },
				[this,taskPtr](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
	} else
	{
		_invalid_operation();
	}
}

VN_IMPL_PACKET_REQUEST2(LoggedUser, pkt,req_unlockRune)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_unlockRune");
	u32 heroId = pkt->data.data.idx;
	u32 runeId = pkt->data.data.runeIdx;
	if (m_bsHero.unlock_rune(heroId, runeId))
	{
		_updateStamp();
		AsyncTaskPtr taskPtr = vnnew AsyncTask();
		taskPtr->Init(2);
		m_bsHero.AsyncsaveRune(heroId, runeId,
				[this,taskPtr,heroId,runeId](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"heroId"<<heroId<<"runId"<<runeId<<"update failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
				this, true }, { this, true },
				[this,taskPtr](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
	} else
	{
		_invalid_operation();
	}

}

//购买钻石显示价格
VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_rmbShopInfo)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_rmbShopInfo");
	const std::map<u32, BSRmbShopManager::Product> & productMap =
			BSRmbShopManager::instance().productMap();

	pkt::res_rmbShopInfo msg;
	msg.data.shopInfo.reserve(productMap.size());
	for (auto &i : productMap)
	{
		if (i.second.hidden)
		{
			continue;
		}
		pkt::Msg_Product product;
		product.id = i.second.id;
		product.price = i.second.price;
		product.count = i.second.count;
		product.name = i.second.name;
		product.desc = i.second.desc;
		msg.data.shopInfo.push_back(product);
	}
	if (m_client)
	{
		m_client->sendPacket(msg);
	}
}

/*
 VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_buyProduct)
 {
 const BSRmbShopManager::Product *product = BSRmbShopManager::instance().find_product(pkt->data.productId);
 if (!product)
 {
 _invalid_operation();
 return ;
 }
 pkt::res_productInfo msg;
 msg.data.productInfo.id = pkt->data.productId;
 msg.data.productInfo.count = product->count;
 msg.data.productInfo.price = product->price;
 //产生订单
 msg.data.productInfo.orderId = DataCenter::instance().generateOrder(m_uuid, pkt->data.productId, product->price);
 msg.data.productInfo.name = product->name;
 msg.data.productInfo.desc = product->desc;
 if (m_client) {
 m_client->sendPacket(msg);
 }
 }*/

//查询余额
VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_getBalance)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_getBalance");
	vn::RefCountedPtr<pkt::req_getBalance> pktPtr(pkt, true);
	PPLinkManger::CallbackFn getBalCallFun =
			[this](vn::s32 result,std::string& context)
			{
				vn::RefCountedPtr<pkt::res_getBalance > msg = vnnew pkt::res_getBalance();
				//解析context
				s32 nResultCode=0;
				vn::Variable_int32 type;
				std::string strKey = "ret";
				if(!ParseJson(&nResultCode,vn::kVT_int32,type,context,strKey))
				{
					VN_LOG_ERROR("Parase FAILED");
					return;
				}
				//-1:can not find player   0:ok
				msg->data.resultCode = nResultCode;
				VN_LOG_DEBUG("nResultCode:"<<nResultCode);

				//获取金币
				s32 nBalance=0;
				//vn::Variable_int32 type;
				strKey = "balance";
				if(!ParseJson(&nBalance,vn::kVT_int32,type,context,strKey))
				{
					VN_LOG_ERROR("Parase FAILED");
					return;
				}
				VN_LOG_DEBUG("nBalance:"<<nBalance);
				msg->data.diamond = nBalance;
				_updateStamp();
				m_bsPlayerBaseInfo.SetDiamond(nBalance);
				((DC_DataBase *)&DataCenter::instance())->AsyncUpdateBaseInfoAll(
						{	this,true},
						{	this,true},[this,msg](bool ret)
						{
							if(!ret)
							{
								VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
							}
							if (m_client)
							{
								m_client->sendPacket(*msg);
							}
						});
			};
	std::string strUUId = GetUUId();
	//查询余额
	sPPLinkMgr.SendGetBalance(
			{	this,true},std::move(getBalCallFun),
			(char*)strUUId.c_str());
}

/*VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_orderNoCheck) {
 if (sWorldManager.instance().buy_nocheck()) {
 order_ok(pkt->data.data);
 } else {
 order_failed(pkt->data.data);
 }
 s32 addDiamond = 0;
 if(!sWorldManager.instance().buy_nocheck())
 {
 order_failed(pkt->data.data);
 }
 else
 {
 order_ok(pkt->data.data,&addDiamond);
 AsyncTaskPtr taskPtr = vnnew AsyncTask();
 taskPtr->Init(1);
 taskPtr->m_diamondCount  = addDiamond;
 ((DC_DataBase *)&DataCenter::instance())->AsyncUpdateBaseInfoAll({this,true},{this,true},[this,taskPtr](bool ret)
 {
 if(!ret)
 {
 VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
 }
 taskPtr->FinishTask(ret);
 _HandlTaskFinish(taskPtr);
 });
 }
 }*/

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_buyItem)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_buyItem");
	s32 payDiamond = 0;
	u32 itemId = pkt->data.data;
	if (m_bsItemStore.buy(itemId, &payDiamond))
	{
		_updateStamp();
		AsyncTaskPtr taskPtr = vnnew AsyncTask();
		taskPtr->Init(2);
		taskPtr->m_diamondCount = payDiamond;
		m_bsItemStore.Asyncsave(itemId,
				[this,taskPtr,itemId](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"itemId"<<itemId<<"update failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
				this, true }, { this, true },
				[this,taskPtr](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
	} else
	{
		_invalid_operation();
	}

}

VN_IMPL_PACKET_REQUEST2(LoggedUser, pkt,req_useItem)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_useItem");
	u32 itemId = pkt->data.data;
	if (m_bsItemStore.use(itemId))
	{
		_updateStamp();
		AsyncTaskPtr taskPtr = vnnew AsyncTask();
		taskPtr->Init(2);
		m_bsItemStore.Asyncsave(itemId,
				[this,taskPtr,itemId](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"itemId"<<itemId<<"update failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
				this, true }, { this, true },
				[this,taskPtr](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
	} else
	{
		_invalid_operation();
	}

}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt, req_meetMonster)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_meetMonster");
	u32 monsterId = pkt->data.data;
	if (!m_bsMonsterMap.meet_monster(monsterId))
	{
		_invalid_operation();
		return;
	} else
	{
		_updateStamp();
		AsyncTaskPtr taskPtr = vnnew AsyncTask();
		taskPtr->Init(2);
		m_bsMonsterMap.Asyncsave(monsterId,
				[this,taskPtr,monsterId](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"monsterId"<<monsterId<<"update failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
				this, true }, { this, true },
				[this,taskPtr](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
	}
}

//强制引导
VN_IMPL_PACKET_REQUEST2(LoggedUser, pkt,req_disableFG)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_disableFG");
	u32 guideId = pkt->data.data;
	if (!m_bsForceGuide.insert(guideId))
	{
		_invalid_operation();
		return;
	} else
	{
		_updateStamp();
		AsyncTaskPtr taskPtr = vnnew AsyncTask();
		taskPtr->Init(2);
		((DC_DataBase *) &DataCenter::instance())->AsyncInsertForceGuide( {
				this, true }, pkt->data.data,
				[this,taskPtr,guideId](bool result)
				{
					if(!result)
					{
						VN_LOG_ERROR("strUUId"<<GetUUId()<<"guideid:"<<guideId<<"failed");
					}
					taskPtr->FinishTask(result);
					_HandlTaskFinish(taskPtr);
				});
		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
				this, true }, { this, true },
				[this,taskPtr](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
	}
}

VN_IMPL_PACKET_REQUEST2(LoggedUser, pkt,req_disableFG_AddItems)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_disableFG_AddItems");
	u32 guideId = pkt->data.data.groupId;
	int taskSize = 2 + pkt->data.data.items.size();
	if (m_bsForceGuide.insert(guideId))
	{
		_updateStamp();
		AsyncTaskPtr taskPtr = vnnew AsyncTask();
		taskPtr->Init(taskSize);
		for (auto &i : pkt->data.data.items)
		{
			u32 itemId = i.id;
			m_bsItemStore._add(itemId, i.count);
			m_bsItemStore.Asyncsave(itemId,
					[this,taskPtr,itemId](bool ret)
					{
						if(!ret)
						{
							VN_LOG_ERROR("UUid"<<GetUUId()<<"itemId"<<itemId<<"update failed");
						}
						taskPtr->FinishTask(ret);
						_HandlTaskFinish(taskPtr);
					});
		}
		((DC_DataBase *) &DataCenter::instance())->AsyncInsertForceGuide( {
				this, true }, guideId,
				[this,guideId,taskPtr](bool result)
				{
					if(!result)
					{
						VN_LOG_ERROR("strUUId"<<GetUUId()<<"guideid:"<<guideId<<"failed");
					}
					taskPtr->FinishTask(result);
					_HandlTaskFinish(taskPtr);
				});

		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
				this, true }, { this, true },
				[this,taskPtr](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
	} else
	{
		_invalid_operation();
	}
}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt, req_buyPlatform)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_buyPlatform");
	s32 payDiamond = 0;
	u32 category = pkt->data.category;
	if (m_bsPlatformStore.buy(category, &payDiamond))
	{
		_updateStamp();
		AsyncTaskPtr taskPtr = vnnew AsyncTask();
		taskPtr->Init(2);
		taskPtr->m_diamondCount = payDiamond;
		m_bsPlatformStore.Asyncsave(category,
				[this,taskPtr,category](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"category:"<<category<<"AsyncUpdatefailed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
				this, true }, { this, true },
				[this,taskPtr](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
	} else
	{
		_invalid_operation();
	}

}

VN_IMPL_PACKET_REQUEST2(LoggedUser, pkt,req_buyHeroWithCoupon)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_buyHeroWithCoupon");
	u32 heroId = pkt->data.data;
	s32 payDiamond = 0;
	if (m_bsHero.upgrade_hero(pkt->data.data, true, &payDiamond))
	{
		_updateStamp();
		AsyncTaskPtr taskPtr = vnnew AsyncTask();
		taskPtr->Init(2);
		taskPtr->m_diamondCount = payDiamond;
		m_bsHero.Asyncsave(heroId,
				[this,taskPtr,heroId](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"heroId"<<heroId<<"update failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
		((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( {
				this, true }, { this, true },
				[this,taskPtr](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});

	} else
	{
		_invalid_operation();
	}

}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_useCoupon)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_useCoupon");
	s32 addDiamond = 0;
	if (!m_coupon)
	{
		_invalid_operation();
	} else
	{
		switch (m_coupon->type())
		{
		case BSCoupon::kCT_Items:
		{	//fix
			if (m_bsPlayerBaseInfo.diamond() < (s32) m_coupon->cost)
			{
				_invalid_operation();
			} else
			{
				//m_bsPlayerBaseInfo.addDiamond((s32)m_coupon->cost);
				addDiamond = (s32) m_coupon->cost;
				auto pc = static_cast<const BSCoupon_Items *>(m_coupon);
				int taskNum = pc->items.size() + 1;
				AsyncTaskPtr taskPtr = vnnew AsyncTask();
				taskPtr->Init(taskNum);
				taskPtr->m_diamondCount = addDiamond;
				_notify_couponUsed();
				_updateStamp();
				for (auto &i : pc->items)
				{
					u32 itemId = i.first;
					m_bsItemStore.Asyncsave(i.first,
							[this,itemId,taskPtr](bool ret)
							{
								if(!ret)
								{
									VN_LOG_ERROR("UUID:"<<GetUUId()<<"itemId"<<itemId<<"update failed");
								}
								taskPtr->FinishTask(ret);
								_HandlTaskFinish(taskPtr);
							});
				}

				((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll(
						{ this, true }, { this, true },
						[this,taskPtr](bool ret)
						{
							if(!ret)
							{
								VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
							}
							taskPtr->FinishTask(ret);
							_HandlTaskFinish(taskPtr);
						});
			}
			break;
		}

		case BSCoupon::kCT_Diamonds:
		{
			auto product =
					BSRmbShopManager::instance().find_product(
							static_cast<const BSCoupon_Diamonds *>(m_coupon)->productId);
			if (product)
			{
				pkt::res_productInfo msg;
				msg.data.productInfo.id = product->id;
				msg.data.productInfo.price = product->price;
				msg.data.productInfo.orderId =
						DataCenter::instance().generateOrder(m_uuid,
								product->id, product->price);
				msg.data.productInfo.name = product->name;
				msg.data.productInfo.desc = product->desc;
				m_client->sendPacket(msg);
			}
			break;
		}

		case BSCoupon::kCT_Hero:
		{
			auto heroClassId =
					static_cast<const BSCoupon_Hero *>(m_coupon)->heroClassId;
			auto &list = m_bsHero.heroList();
			bool ok = false;
			u32 heroIndex;
			for (u32 i = 0, c = (u32) list.size(); i < c; ++i)
			{
				if (list[i].towerClass == heroClassId)
				{
					heroIndex = i;
					ok = m_bsHero.upgrade_hero(i, true, &addDiamond);
					_updateStamp();
					break;
				}
			}
			if (!ok)
			{
				_invalid_operation();
				return;
			} else
			{

				AsyncTaskPtr taskPtr = vnnew AsyncTask();
				taskPtr->Init(2);
				taskPtr->m_diamondCount = addDiamond;
				m_bsHero.Asyncsave(heroIndex,
						[this,taskPtr,heroIndex](bool ret)
						{
							if(!ret)
							{
								VN_LOG_ERROR("UUid"<<GetUUId()<<"heroIndex"<<heroIndex<<"update failed");
							}
							taskPtr->FinishTask(ret);
							_HandlTaskFinish(taskPtr);
						});
				((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll(
						{ this, true }, { this, true },
						[this,taskPtr](bool ret)
						{
							if(!ret)
							{
								VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
							}
							taskPtr->FinishTask(ret);
							_HandlTaskFinish(taskPtr);
						});
			}
			break;
		}
		default:
			_invalid_operation();
			break;
		}
	}
}

//请求抽奖
VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt, req_lotteryDraw)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_lotteryDraw");
	if (m_lotteryState != kLottery_Opened || m_lotteryPool.empty() )
	{
		_invalid_operation();
		return;
	}
	AsyncTaskPtr task = vnnew AsyncTask();
	s32 addDiamonds = 0;
	task->Init(1);
	if (m_lotteryDrawTimes)
	{
		--m_lotteryDrawTimes;
	}
	//如果用光了免费的次数
	else if (m_bsPlayerBaseInfo.diamond() < sWorldManager.instance().lottery_drawCost())
	{
		_invalid_operation();
		return;
	} else
	{
		//m_bsPlayerBaseInfo.addDiamond(-(s32)sWorldManager.instance().lottery_drawCost());
		addDiamonds +=-(s32)sWorldManager.instance().lottery_drawCost();
	}
	const BSAward *award = 0;
	u32 idx = BSAwardManager::instance().draw(m_lotteryPool, award);
	if (!award || idx >= m_lotteryPool.size())
	{
		_invalid_operation();
		return;
	}
	_updateStamp();

	auto &p = m_lotteryPool[idx];
	if (p.first->money)
	{
		m_bsPlayerBaseInfo.addMoney(p.first->money);
	}
	if (p.first->diamonds)
	{
		// m_bsPlayerBaseInfo.addDiamond(p.first->diamonds);
		addDiamonds += p.first->diamonds;
	}
	for (auto &i : p.first->items)
	{
		m_bsItemStore._add(i.first, i.second);
	}
	p.first = award;
	p.second = award->weight;
	u32 rank = award->rank;
	u32 imageId = award->imageId;
	str8 desc = award->desc;
	s32 money = award->money;
	s32 diamonds = award->diamonds;
	std::vector<std::pair<u32, u32>> items = award->items;
	task->m_diamondCount = addDiamonds;
	pkt::res_lotteryDrawInfo res;
	res.data.data.index = idx;
	res.data.data.newAward.rank = award->rank;
	res.data.data.newAward.imageId = award->imageId;
	res.data.data.newAward.desc = award->desc;
	res.data.data.newAward.money = award->money;
	res.data.data.newAward.diamonds = award->diamonds;
	res.data.data.newAward.items.reserve(award->items.size());
	for (auto &i : award->items)
	{
		res.data.data.newAward.items.push_back( { i.first, i.second });
	}
	m_client->sendPacket(res);
	((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( { this,
			true }, { this, true },
			[this,rank,idx,imageId,desc,money,diamonds,items,addDiamonds,task](bool ret)
			{
				if(!ret)
				{
					VN_LOG_ERROR("UUid"<<GetUUId()<<"AsyncUpdateBaseInfoAll failed");
				}
				task->FinishTask(ret);
				_HandlTaskFinish(task);
			});
}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_buyMoney)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_buyMoney");
	s32 payDiamond = 0;
	const BSDiamondShop::Product *product =
			BSDiamondShop::instance().find_product(pkt->data.data);
	if (!product)
	{
		_invalid_operation();
		return;
	}
//fix
	if (m_bsPlayerBaseInfo.diamond() < (s32) product->price)
	{
		_invalid_operation();
		return;
	}
	m_bsPlayerBaseInfo.addMoney((s32) product->count);
	// m_bsPlayerBaseInfo.addDiamond(-(s32)product->price);
	payDiamond = -(s32) product->price;
	_updateStamp();
	AsyncTaskPtr taskPtr = vnnew AsyncTask();
	taskPtr->m_diamondCount = payDiamond;
	taskPtr->Init(1);
	((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( { this,
			true }, { this, true }, [this,taskPtr](bool result)
	{
		if(!result)
		{
			VN_LOG_ERROR("uuid"<<GetUUId()<<":UpdateBaseInfo "<<"failed");
		}
		taskPtr->FinishTask(result);
		_HandlTaskFinish(taskPtr);
		//todo..结束事务
		});
}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_getMissionReward)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_getMissionReward");
	u32 missionId = pkt->data.data;
	s32 addDiamond = 0;
	const BSMissionManager::Mission *mission =
			BSMissionManager::instance().find_mission(missionId);
	if (mission == nullptr)
	{
		VN_LOG_ERROR("missionId error!! update table. !!");
		_invalid_operation();
		return;
	}

	if (mission->levelType == 1)
	{
		//level
		auto info = m_bsLevel.find(mission->level);
		if (info == nullptr || info->starNum == 0)
		{
			_invalid_operation();
			return;
		}
		//mission->level
	} else if (mission->levelType == 2)
	{
		//levelGroup
		u32 status = m_bsLevelGroupMgr.checkGroupStatus(mission->level);
		if (status == 100 || status == 0)
		{
			_invalid_operation();
			return;
		}
	}

	auto i = m_bsMission.list().find(pkt->data.data);

	if (i->second)
	{
		_invalid_operation();
		return;
	}
	bool ifUpdateItemStore = false;
	u32 rewardType = mission->rewardType;
	switch (mission->rewardType)
	{
	case 1:
		m_bsPlayerBaseInfo.addMoney(mission->rewardNum);
		break;
	case 2:
		// m_bsPlayerBaseInfo.addDiamond(mission->rewardNum);
		addDiamond = mission->rewardNum;
		break;
	case 3:
		m_bsPlayerBaseInfo.addChip(0, mission->rewardNum);
		break;
	case 4:
		m_bsPlayerBaseInfo.addChip(1, mission->rewardNum);
		break;
	case 5:
		m_bsPlayerBaseInfo.addChip(2, mission->rewardNum);
		break;
	case 910:
		m_bsItemStore._add(910, mission->rewardNum);
		ifUpdateItemStore = true;
		break;
	case 920:
		m_bsItemStore._add(920, mission->rewardNum);
		ifUpdateItemStore = true;
		break;
	case 930:
		m_bsItemStore._add(930, mission->rewardNum);
		ifUpdateItemStore = true;
		break;
	case 940:
		m_bsItemStore._add(940, mission->rewardNum);
		ifUpdateItemStore = true;
		break;
	default:
		break;
	}
	_updateStamp();
	m_bsMission.setReward(pkt->data.data, true);

	AsyncTaskPtr taskPtr = vnnew AsyncTask();
	taskPtr->m_diamondCount = addDiamond;
	if (ifUpdateItemStore)
	{
		taskPtr->Init(3);
		m_bsItemStore.Asyncsave(rewardType,
				[this,rewardType,taskPtr](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUId"<<GetUUId()<<"itemId:"<<rewardType<<"update failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
	} else
	{
		taskPtr->Init(2);
	}
	m_bsMission.Asyncsave(missionId,
			[this,taskPtr,missionId](bool ret)
			{
				if(!ret)
				{
					VN_LOG_ERROR("UUId"<<GetUUId()<<"missionId:"<<missionId<<"update failed");
				}
				taskPtr->FinishTask(ret);
				_HandlTaskFinish(taskPtr);
			});
	((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( { this,
			true }, { this, true }, [this,taskPtr](bool ret)
	{
		if(!ret)
		{
			VN_LOG_ERROR("uuid"<<GetUUId()<<":UpdateBaseInfo "<<"failed");
			taskPtr->FinishTask(ret);
		}
		taskPtr->FinishTask(ret);
		_HandlTaskFinish(taskPtr);
	});
}

//签到奖励
VN_IMPL_PACKET_REQUEST2(LoggedUser, pkt,req_getRegistrationReward)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_getRegistrationReward");
	s32 addDiamond = 0;
	Date today = Date::today();
	++today.month;
	if (m_bsRegistration.m_lastDate == today)
	{
		_invalid_operation();
		return;
	} else if (m_bsRegistration.m_firstDate.year == 0)
	{
		m_bsRegistration.m_firstDate = today;
		m_bsRegistration.m_lastDate = today;
		m_bsRegistration.m_nums = 1;
		m_bsRegistration.m_group = 0;
	} else
	{
		u32 d = BSRegistrationManager::instance().daysBetween2Date(
				m_bsRegistration.m_firstDate, today);
		++d;
		if (d > 7)
		{
			m_bsRegistration.m_firstDate = today;
			m_bsRegistration.m_nums = 1;
			++m_bsRegistration.m_group;
			m_bsRegistration.m_group %= 4;
		} else
		{
			++m_bsRegistration.m_nums;
		}
	}
	auto rewardGoups = BSRegistrationManager::instance().rewardGroups();

	auto rg = rewardGoups[m_bsRegistration.m_group].find(
			m_bsRegistration.m_nums);
	if (rg == rewardGoups[m_bsRegistration.m_group].end())
	{
		_invalid_operation();
		return;
	}
	u32 rewardType = rg->second.rewardTheprops;
	switch (rewardType)
	{
	case 1:
		m_bsPlayerBaseInfo.addMoney(rg->second.rewardNumber);
		break;
	case 2:
		// m_bsPlayerBaseInfo.addDiamond(rg->second.rewardNumber);
		addDiamond += rg->second.rewardNumber;
		break;
	case 3:
		m_bsPlayerBaseInfo.addChip(0, rg->second.rewardNumber);
		break;
	case 4:
		m_bsPlayerBaseInfo.addChip(1, rg->second.rewardNumber);
		break;
	case 5:
		m_bsPlayerBaseInfo.addChip(2, rg->second.rewardNumber);
		break;
	case 6:
		m_lotteryDrawTimes += rg->second.rewardNumber;
		break;
	case 910:
		m_bsItemStore._add(910, rg->second.rewardNumber);
		break;
	case 920:
		m_bsItemStore._add(920, rg->second.rewardNumber);
		break;
	case 930:
		m_bsItemStore._add(930, rg->second.rewardNumber);
		break;
	case 940:
		m_bsItemStore._add(940, rg->second.rewardNumber);
		break;
	default:
		break;
	}
	_updateStamp();
	m_bsRegistration.m_lastDate = today;
	AsyncTaskPtr taskPtr = vnnew AsyncTask();
	taskPtr->m_diamondCount = addDiamond;
	if (rewardType == 910 || rewardType == 920 || rewardType == 930
			|| rewardType == 940)
	{
		taskPtr->Init(3);
		m_bsItemStore.Asyncsave(rewardType,
				[this,rewardType,taskPtr](bool ret)
				{
					if(!ret)
					{
						VN_LOG_ERROR("UUID"<<GetUUId()<<"itemId"<<rewardType<<"update itemStore failed");
					}
					taskPtr->FinishTask(ret);
					_HandlTaskFinish(taskPtr);
				});
	} else
	{
		taskPtr->Init(2);
	}
	//更新注册信息
	bsRegistration().Asyncsave([this,taskPtr](bool ret)
	{
		if(!ret)
		{
			VN_LOG_ERROR("UUID"<<GetUUId()<<"update Registration failed");
		}
		taskPtr->FinishTask(ret);
		_HandlTaskFinish(taskPtr);
	});
	((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( { this,
			true }, { this, true }, [this,taskPtr](bool ret)
	{
		if(!ret)
		{
			VN_LOG_ERROR("uuid"<<GetUUId()<<":UpdateBaseInfo "<<"failed");
		}
		taskPtr->FinishTask(ret);
		_HandlTaskFinish(taskPtr);
	});

}

VN_IMPL_PACKET_REQUEST2(LoggedUser, pkt,req_redeemCode)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_redeemCode");
	//todo
	return;
	auto rp = DataCenter::instance().queryRedeemPacket(pkt->data.data);
	do
	{
		if (!rp)
		{
			break;
		}
		if (m_redeemPacketCategories.find(rp->category)
				!= m_redeemPacketCategories.end())
		{
			break;
		}
		rp->_lock.lock();
		if (!rp->_valid)
		{
			rp->_lock.unlock();
			break;
		}
		rp->_valid = false;
		rp->_lock.unlock();
		DataCenter::instance().removeRedeemPacket(rp->codeId, m_uuid);

		m_redeemPacketCategories.insert(rp->category);
		m_bsPlayerBaseInfo.addMoney(rp->money);
		m_bsPlayerBaseInfo.addDiamond(rp->diamonds);
		_updateStamp();

		if (m_client)
		{
			pkt::res_redeemCodePacket res;
			res.data.data.title =
					DataCenter::instance().queryRedeemPacketCategoryName(
							rp->category);
			res.data.data.money = rp->money;
			res.data.data.diamonds = rp->diamonds;
			m_client->sendPacket(res);
		}
		return;
	} while (false);

	// invalid
	if (m_client)
	{
		pkt::res_redeemCodeInvalid res;
		m_client->sendPacket(res);
	}
}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_add_money)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_add_money");
	m_bsPlayerBaseInfo.addMoney((s32) pkt->data.data);
	_updateStamp();
	((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( { this,
			true }, { this, true }, [this](bool ret)
	{
		if(!ret)
		{
			VN_LOG_ERROR("uuid"<<GetUUId()<<":UpdateBaseInfo "<<"failed");
			DbOperFailHandl();
			return;
		}
	});
	return;
}

VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_add_diamond)
{

	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_add_diamond");
	_updateStamp();
	AsyncTaskPtr taskPtr = vnnew AsyncTask();
	taskPtr->Init(1);
	taskPtr->m_diamondCount = (s32) pkt->data.data;
	((DC_DataBase *) &DataCenter::instance())->AsyncUpdateBaseInfoAll( { this,
			true }, { this, true }, [this,taskPtr](bool ret)
	{
		if(!ret)
		{
			VN_LOG_ERROR("uuid"<<GetUUId()<<":UpdateBaseInfo "<<"failed");
		}
		taskPtr->FinishTask(ret);
		_HandlTaskFinish(taskPtr);
	});

}

//地图全开
VN_IMPL_PACKET_REQUEST2(LoggedUser,pkt,req_pass_level)
{
	VN_LOG_DEBUG("uuid:"<<m_uuid<<"req_pass_level");
	//todo
	return;
	_invalid_operation();
	_updateStamp();
	m_bsLevel.pass_level(pkt->data.data);
	m_bsHero.open_hero();
	m_bsIntrusion.intrusion();
	_checkLottery(pkt->data.data);
}

/*void LoggedUser::AsyncUpdateInstrusion(bool running,f32 timer)
 {
 std::string strUUId = m_uuid.to_string();
 PreparedStatement*  stmt=PassPortDatabase.GetPreparedStatement(UP_INTRUSION);
 stmt->setString(0,strUUId);
 stmt->setBool(1,running);
 stmt->setBool(2,timer);
 PassPortDatabase.AsyncExecute(stmt,{this,true},[&](bool result)
 {
 if(!result)
 {
 VN_LOG_ERROR(strUUId<<":UpdateInstrusion "<<"failed");
 }
 });
 }*/

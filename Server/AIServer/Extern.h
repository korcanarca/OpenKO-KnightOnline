#ifndef _EXTERN_H_
#define _EXTERN_H_

// -------------------------------------------------
// 전역 객체 변수
// -------------------------------------------------
extern BOOL	g_bNpcExit;

import AIServerModel;
namespace model = aiserver_model;

struct	_PARTY_GROUP
{
	WORD wIndex;
	short uid[8];		// 하나의 파티에 8명까지 가입가능
	_PARTY_GROUP()
	{
		for (int i = 0;i < 8;i++)
			uid[i] = -1;
	}
};

struct _MAKE_WEAPON
{
	BYTE	byIndex;		// 몹의 레벨 기준
	short	sClass[MAX_UPGRADE_WEAPON];		// 1차무기 확률
	_MAKE_WEAPON()
	{
		for (int i = 0;i < MAX_UPGRADE_WEAPON;i++)
			sClass[i] = 0;
	}
};

struct _MAKE_ITEM_GRADE_CODE
{
	BYTE	byItemIndex;		// item grade
	short	sGrade_1;			// 단계별 확률
	short	sGrade_2;
	short	sGrade_3;
	short	sGrade_4;
	short	sGrade_5;
	short	sGrade_6;
	short	sGrade_7;
	short	sGrade_8;
	short	sGrade_9;
};

struct _MAKE_ITEM_LARE_CODE
{
	BYTE	byItemLevel;			// item level 판단 
	short	sLareItem;				// lareitem 나올 확률
	short	sMagicItem;				// magicitem 나올 확률
	short	sGereralItem;			// gereralitem 나올 확률
};

struct	_USERLOG
{
	CTime t;
	BYTE  byFlag;	// 
	BYTE  byLevel;
	char  strUserID[MAX_ID_SIZE + 1];		// 아이디(캐릭터 이름)
};

#endif

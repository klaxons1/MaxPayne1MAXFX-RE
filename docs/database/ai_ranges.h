#define	HEALTH_CLASS_1			15
#define	HEALTH_CLASS_2			20
#define	HEALTH_CLASS_3			25
#define	HEALTH_CLASS_4			30
#define	HEALTH_CLASS_5			35
#define	HEALTH_CLASS_6			40
#define	HEALTH_CLASS_7			45
#define	HEALTH_CLASS_8			70
#define	HEALTH_CLASS_9			130
#define	HEALTH_CLASS_10			240

#define	WOUNDEDHEALTH_CLASS_1	10
#define	WOUNDEDHEALTH_CLASS_2	9
#define	WOUNDEDHEALTH_CLASS_3	8
#define	WOUNDEDHEALTH_CLASS_4	7
#define	WOUNDEDHEALTH_CLASS_5	6
#define	WOUNDEDHEALTH_CLASS_6	5
#define	WOUNDEDHEALTH_CLASS_7	4
#define	WOUNDEDHEALTH_CLASS_8	15
#define	WOUNDEDHEALTH_CLASS_9	25 
#define	WOUNDEDHEALTH_CLASS_10	50 

#define	SHOOTRATE_CLASS_1		0.10
#define	SHOOTRATE_CLASS_2		0.20
#define	SHOOTRATE_CLASS_3		0.30
#define	SHOOTRATE_CLASS_4		0.40
#define	SHOOTRATE_CLASS_5		0.50
#define	SHOOTRATE_CLASS_6		0.60
#define	SHOOTRATE_CLASS_7		0.70

// Just for reference, actual timings come from animations
// GETHIT_ANIMLENGTH_CLASS_1		1.25
// GETHIT_ANIMLENGTH_CLASS_2		1.00
// GETHIT_ANIMLENGTH_CLASS_3		0.80
// GETHIT_ANIMLENGTH_CLASS_4		0.65
// GETHIT_ANIMLENGTH_CLASS_5		0.50
// GETHIT_ANIMLENGTH_CLASS_6		0.35
// GETHIT_ANIMLENGTH_CLASS_7		0.20

// ------------------------------
// Junkie
// ------------------------------
#define AI_JUNKIE_AIMING_SPEED 							8
#define AI_JUNKIE_AIMING_SPEEDCONE						25
#define AI_JUNKIE_GROUP_ONE								15
#define AI_JUNKIE_GROUP_TWO								4
#define AI_JUNKIE_GROUP_THREE							0
#define AI_JUNKIE_INTEREST_TIME							10
#define AI_JUNKIE_GENERAL_RADIUS						2
#define AI_JUNKIE_VISUAL_RADIUS							20
#define AI_JUNKIE_REACTION_TIME							0.0
#define AI_JUNKIE_SHOOTING_CONE							170
#define AI_JUNKIE_TURNING_SPEED 						80 
#define AI_JUNKIE_ACTIVATE_OTHER_CHARACTERS_RADIUS		0
#define AI_JUNKIE_SHOOTING_FREQUENCY					0.8

// ------------------------------
// Mobster AI
// ------------------------------

// Defines how fast the enemy tracks player, when withing shooting cone
#define AI_MOBSTER_AIMING_SPEED 	10	

// Defines the angle within which to use aiming speed
#define AI_MOBSTER_AIMING_SPEEDCONE	30 	

// Alerts the AI if player is: Shooting
#define AI_MOBSTER_GROUP_ONE		15 	

// Alerts the AI if player is: Running, strafing
#define AI_MOBSTER_GROUP_TWO		4 	

// Alerts the AI if player is:  Peeking
#define AI_MOBSTER_GROUP_THREE		0 	

// Long long the AI keep trying to find the player after activated
#define AI_MOBSTER_INTEREST_TIME	30

// If the player sneeks behind the AI, how near the player can get until the AI reacts
#define AI_MOBSTER_GENERAL_RADIUS	2   

// How far the AI can see
#define AI_MOBSTER_VISUAL_RADIUS	50  

// How long the AI waits idle after getting "activate"
#define AI_MOBSTER_REACTION_TIME	0.0

// When the AI has aimed this close to the "exact" hit, he can start shooting already
#define AI_MOBSTER_SHOOTING_CONE	120  

// Defines AI's turning speed when not withing Shooting Cone
#define AI_MOBSTER_TURNING_SPEED 	520 

// How far the character automatically activates others
#define AI_MOBSTER_ACTIVATE_OTHER_CHARACTERS_RADIUS		0

// Defines how fast the AI shoots (maximum is 1.0), depends heavily on the weapons they carry
#define AI_MOBSTER_SHOOTING_FREQUENCY					0.8

// ------------------------------
// Mercenary Soldier
// ------------------------------
#define AI_MERCENARY_SOLDIER_AIMING_SPEED 	  				10
#define AI_MERCENARY_SOLDIER_AIMING_SPEEDCONE 				30
#define AI_MERCENARY_SOLDIER_GROUP_ONE		  				40
#define AI_MERCENARY_SOLDIER_GROUP_TWO		  				5
#define AI_MERCENARY_SOLDIER_GROUP_THREE	  				2
#define AI_MERCENARY_SOLDIER_INTEREST_TIME	  				30
#define AI_MERCENARY_SOLDIER_GENERAL_RADIUS	  				3
#define AI_MERCENARY_SOLDIER_VISUAL_RADIUS	  				60
#define AI_MERCENARY_SOLDIER_REACTION_TIME	  				0.0
#define AI_MERCENARY_SOLDIER_SHOOTING_CONE	  				100
#define AI_MERCENARY_SOLDIER_TURNING_SPEED 	  				720
#define AI_MERCENARY_SOLDIER_ACTIVATE_OTHER_CHARACTERS_RADIUS		0
#define AI_MERCENARY_SOLDIER_SHOOTING_FREQUENCY				0.4

// ------------------------------
// Killer Suit
// ------------------------------
#define AI_KILLER_SUIT_AIMING_SPEED 					16
#define AI_KILLER_SUIT_AIMING_SPEEDCONE					15
#define AI_KILLER_SUIT_GROUP_ONE						50
#define AI_KILLER_SUIT_GROUP_TWO						8
#define AI_KILLER_SUIT_GROUP_THREE						3
#define AI_KILLER_SUIT_INTEREST_TIME					30
#define AI_KILLER_SUIT_GENERAL_RADIUS					5
#define AI_KILLER_SUIT_VISUAL_RADIUS					70
#define AI_KILLER_SUIT_REACTION_TIME					0.0
#define AI_KILLER_SUIT_SHOOTING_CONE					10
#define AI_KILLER_SUIT_TURNING_SPEED 					720
#define AI_KILLER_SUIT_ACTIVATE_OTHER_CHARACTERS_RADIUS	0
#define AI_KILLER_SUIT_SHOOTING_FREQUENCY				0.7

// ------------------------------
// Boss 
// ------------------------------

#define AI_BOSS_INTEREST_TIME							360 

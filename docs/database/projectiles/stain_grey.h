// -------------------------------------------------------------------
// Stain_Grey_Small, Stain_Grey_Medium, Stain_Grey_Large
// -------------------------------------------------------------------

[Attributes]
Direction 	= (0,0,0);
Accuracy 	= 2000;
Speed 		= 1;
SpeedRandom = 30%;
Damage 		= 0;			 

RicochetMaximumAngle 	= 0;
RicochetSpeedMultiplier = 0.3;
RicochetMinimumSpeed 	= 2;
RicochetUntilOnGround	= false; // true for projectiles that create level items
VisibilityMaximumSpeed  = 30;

SmallProjectile	 		= TRUE;
Debris 					= TRUE;
RandomizeDirection 		= FALSE;
RandomizeRotation		= FALSE;	

CollisionExplosion		= FALSE;
DelayedExplosion 		= FALSE;
ExplosionDelay			= 0.0;
GravityMultiplier		= 1.0;
UseLookAt				= FALSE;
DamagesCharacter		= FALSE;
ItemsToCache			= 1;
MeshesToCache			= 1;
DeathCause				= PROJECTILEDEATH_RANDOM;
AdultContent			= FALSE;
BloodProjectile			= FALSE;

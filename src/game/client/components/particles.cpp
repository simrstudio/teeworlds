/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <engine/graphics.h>
#include <engine/demo.h>

#include <game/generated/client_data.h>
#include <game/client/render.h>
#include <game/gamecore.h>
#include "particles.h"

CParticles::CParticles()
{
	OnReset();
	m_RenderTrail.m_pParts = this;
	m_RenderExplosions.m_pParts = this;
	m_RenderGeneral.m_pParts = this;
}


void CParticles::OnReset()
{
	// reset particles
	for(int i = 0; i < MAX_PARTICLES; i++)
	{
		m_aParticles[i].m_PrevPart = i-1;
		m_aParticles[i].m_NextPart = i+1;
	}
	
	m_aParticles[0].m_PrevPart = 0;
	m_aParticles[MAX_PARTICLES-1].m_NextPart = -1;
	m_FirstFree = 0;

	for(int i = 0; i < NUM_GROUPS; i++)
		m_aFirstPart[i] = -1;
	
	ResetExplosions();
}

void CParticles::Add(int Group, CParticle *pPart)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		const IDemoPlayer::CInfo *pInfo = DemoPlayer()->BaseInfo();		
		if(pInfo->m_Paused)
			return;
	}

	if (m_FirstFree == -1)
		return;
		
	// remove from the free list
	int Id = m_FirstFree;
	m_FirstFree = m_aParticles[Id].m_NextPart;
	if(m_FirstFree != -1)
		m_aParticles[m_FirstFree].m_PrevPart = -1;
	
	// copy data
	m_aParticles[Id] = *pPart;
	
	// insert to the group list
	m_aParticles[Id].m_PrevPart = -1;
	m_aParticles[Id].m_NextPart = m_aFirstPart[Group];
	if(m_aFirstPart[Group] != -1)
		m_aParticles[m_aFirstPart[Group]].m_PrevPart = Id;
	m_aFirstPart[Group] = Id;
	
	// set some parameters
	m_aParticles[Id].m_Life = 0;
}

void CParticles::AddExplosion(vec2 Pos)
{
	if(m_ExplosionCount != MAX_PROJECTILES)
		m_aExplosionPos[m_ExplosionCount++] = Pos;
}

void CParticles::ResetExplosions()
{
	mem_zero(&m_aExplosionPos, sizeof(m_aExplosionPos));
	m_ExplosionCount = 0;
}
	
void CParticles::Update(float TimePassed)
{
	static float FrictionFraction = 0;
	FrictionFraction += TimePassed;

	if(FrictionFraction > 2.0f) // safty messure
		FrictionFraction = 0;
	
	int FrictionCount = 0;
	while(FrictionFraction > 0.05f)
	{
		FrictionCount++;
		FrictionFraction -= 0.05f;
	}
	
	float IntraTick = Client()->PredIntraGameTick();
	int NumItems = Client()->SnapNumItems(IClient::SNAP_CURRENT);
	
	for(int g = 0; g < NUM_GROUPS; g++)
	{
		int i = m_aFirstPart[g];
		while(i != -1)
		{
			int Next = m_aParticles[i].m_NextPart;
			
			if(m_aParticles[i].m_FlowAffected)
			{
				// check against players
				for(int j = 0; j < MAX_CLIENTS; j++)
				{
					if(!m_pClient->m_Snap.m_aCharacters[j].m_Active)
						continue;
					
					CNetObj_Character *pCur = &m_pClient->m_Snap.m_aCharacters[j].m_Cur;
					CNetObj_Character *pPrev = &m_pClient->m_Snap.m_aCharacters[j].m_Prev;
					vec2 Position = mix(vec2(pPrev->m_X, pPrev->m_Y), vec2(pCur->m_X, pCur->m_Y), IntraTick);
					vec2 Vel = mix(vec2(pPrev->m_VelX/256.0f, pPrev->m_VelY/256.0f), vec2(pCur->m_VelX/256.0f, pCur->m_VelY/256.0f), IntraTick);
					float VelLength = clamp(length(Vel), 0.0f, 50.0f);
					if(distance(Position, m_aParticles[i].m_Pos) < 28.0f)
						m_aParticles[i].m_Vel += Vel*(((1-(VelLength/40.0f)*m_aParticles[i].m_FlowAffected))+0.05f)*TimePassed*500.0f;
				}
			
				// check against explosions
				for(int j = 0; j < m_ExplosionCount; j++)
				{
					float Distance = distance(m_aParticles[i].m_Pos, m_aExplosionPos[j]);
					if(Distance < 82.0f && Distance > 0.0f)
					{
						vec2 Dir = normalize(m_aParticles[i].m_Pos-m_aExplosionPos[j]);
						m_aParticles[i].m_Vel += Dir*500.0f*((82.0f-Distance)/82.0f)*TimePassed*500.0f;
					}
				}
				
				// check against projectiles
				for(int j = 0; j < NumItems; j++)
				{
					IClient::CSnapItem Item;
					const void *pData = Client()->SnapGetItem(IClient::SNAP_CURRENT, j, &Item);

					if(Item.m_Type == NETOBJTYPE_PROJECTILE)
					{
						const CNetObj_Projectile *pProj = (const CNetObj_Projectile *)pData;
						
						// get positions
						float Curvature = 0;
						float Speed = 0;
						if(pProj->m_Type == WEAPON_GRENADE)
						{
							Curvature = m_pClient->m_Tuning.m_GrenadeCurvature;
							Speed = m_pClient->m_Tuning.m_GrenadeSpeed;
						}
						else if(pProj->m_Type == WEAPON_SHOTGUN)
						{
							Curvature = m_pClient->m_Tuning.m_ShotgunCurvature;
							Speed = m_pClient->m_Tuning.m_ShotgunSpeed;
						}
						else if(pProj->m_Type == WEAPON_GUN)
						{
							Curvature = m_pClient->m_Tuning.m_GunCurvature;
							Speed = m_pClient->m_Tuning.m_GunSpeed;
						}

						float Ct = (Client()->PrevGameTick()-pProj->m_StartTick)/(float)SERVER_TICK_SPEED + Client()->GameTickTime();
						if(Ct < 0)
							return; // projectile havn't been shot yet
						
						vec2 StartPos(pProj->m_X, pProj->m_Y);
						vec2 StartVel(pProj->m_VelX/100.0f, pProj->m_VelY/100.0f);
						vec2 Pos = CalcPos(StartPos, StartVel, Curvature, Speed, Ct);
						vec2 PrevPos = CalcPos(StartPos, StartVel, Curvature, Speed, Ct-0.001f);
						vec2 Vel = Pos-PrevPos;
						if(distance(Pos, m_aParticles[i].m_Pos) < 16.0f)
							m_aParticles[i].m_Vel += Vel*10.0f*m_aParticles[i].m_FlowAffected*TimePassed*500.0f;
					}
				}
			}
			
			m_aParticles[i].m_Vel.y += m_aParticles[i].m_Gravity*TimePassed;
			
			for(int f = 0; f < FrictionCount; f++) // apply friction
				m_aParticles[i].m_Vel *= m_aParticles[i].m_Friction;
			
			// move the point
			vec2 Vel = m_aParticles[i].m_Vel*TimePassed;
			Collision()->MovePoint(&m_aParticles[i].m_Pos, &Vel, 0.1f+0.9f*frandom(), NULL);
			m_aParticles[i].m_Vel = Vel* (1.0f/TimePassed);
			
			m_aParticles[i].m_Life += TimePassed;
			m_aParticles[i].m_Rot += TimePassed * m_aParticles[i].m_Rotspeed;

			// check particle death
			if(m_aParticles[i].m_Life > m_aParticles[i].m_LifeSpan)
			{
				// remove it from the group list
				if(m_aParticles[i].m_PrevPart != -1)
					m_aParticles[m_aParticles[i].m_PrevPart].m_NextPart = m_aParticles[i].m_NextPart;
				else
					m_aFirstPart[g] = m_aParticles[i].m_NextPart;
					
				if(m_aParticles[i].m_NextPart != -1)
					m_aParticles[m_aParticles[i].m_NextPart].m_PrevPart = m_aParticles[i].m_PrevPart;
					
				// insert to the free list
				if(m_FirstFree != -1)
					m_aParticles[m_FirstFree].m_PrevPart = i;
				m_aParticles[i].m_PrevPart = -1;
				m_aParticles[i].m_NextPart = m_FirstFree;
				m_FirstFree = i;
			}
			
			i = Next;
		}
	}
	
	// reset explosion count
	m_ExplosionCount = 0;
}

void CParticles::OnRender()
{
	static int64 LastTime = 0;
	int64 t = time_get();
	
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		const IDemoPlayer::CInfo *pInfo = DemoPlayer()->BaseInfo();		
		if(!pInfo->m_Paused)
			Update((float)((t-LastTime)/(double)time_freq())*pInfo->m_Speed);
	}
	else
		Update((float)((t-LastTime)/(double)time_freq()));
	
	LastTime = t;
}

void CParticles::RenderGroup(int Group)
{
	Graphics()->BlendNormal();
	//gfx_blend_additive();
	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_PARTICLES].m_Id);
	Graphics()->QuadsBegin();

	int i = m_aFirstPart[Group];
	while(i != -1)
	{
		RenderTools()->SelectSprite(m_aParticles[i].m_Spr);
		float a = m_aParticles[i].m_Life / m_aParticles[i].m_LifeSpan;
		vec2 p = m_aParticles[i].m_Pos;
		float Size = mix(m_aParticles[i].m_StartSize, m_aParticles[i].m_EndSize, a);

		Graphics()->QuadsSetRotation(m_aParticles[i].m_Rot);

		Graphics()->SetColor(
			m_aParticles[i].m_Color.r,
			m_aParticles[i].m_Color.g,
			m_aParticles[i].m_Color.b,
			m_aParticles[i].m_Color.a); // pow(a, 0.75f) * 

		IGraphics::CQuadItem QuadItem(p.x, p.y, Size, Size);
		Graphics()->QuadsDraw(&QuadItem, 1);
		
		i = m_aParticles[i].m_NextPart;
	}
	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();
}

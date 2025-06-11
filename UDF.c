#include "udf.h"
#include "mem.h"
#include "sg_mphase.h"
#include "sg.h"
#include "flow.h"
#include "metric.h"
#include "surf.h"
//#include "math.h"
#include "stdio.h"

#define Q_input 3500
#define eta 0.70 //热源效率
#define Q (eta*Q_input) //实际功率

#define v 180.0e-3		 //焊接速度
#define H 1.e-3  		 //板材厚度
#define H_air_down  0.5e-3 //下板材厚度
#define H_air_up  0.5e-3 //上板材厚度

#define re 0.25e-3 //有效热源半径：上表面开口半径
#define ri 0.125e-3 //有效热源半径：下表面开口半径
#define ze 1.5e-3 //上表面高度
#define zi 0.5e-3 //下表面高度
#define Chi 1.4 //热源沿深度方向变化比例系数
//#define log_Chi 0.079

#define Grid_length 0.05e-3
#define Boerz 5.67e-8 //玻尔兹曼常数
#define FI 0.4 // 辐射率系数
#define temp0 300 //环境温度
#define cof_h 1.7 // 蒸发热源系数 
#define hcov 50 // 对流散热系数

#define L_start_point (1.e-3)  //焊接起始位置
#define Meltingt 860  //熔点没用到。 liquidus: 864 solidus: 911
#define g 9.8 // 重力加速度
#define beta 2.3e-5  //热膨胀系数
#define E 1.6e5  


#define Tl 911.0  //液相线
#define Ts 864.0//固相线

//#define T0 70.0  //
#define PI 3.14

#define small 1.e-8  //极小量
#define T_SAT 2790  //沸点

#define LAT_HT  5.03e5  //熔化潜热
#define EVAPORATION_LAT 1.07e7 //蒸发潜热
#define molemass  27e-3//27e-3
#define CONSTANT_RECOIL 0.54
#define Al_density 2690.0
#define Ave_density 1345.0

#define L_Al_Vis 4.2e-3  //液相铝粘度  1.2e-3   Reference Data for the Density and Viscosity of Liquid Aluminum and Liquid Iron
                          //4.2e-3 Numerical Simulation of Spatter Formation During Fiber Laser Welding of 5083 Aluminum Alloy at Full Penetration Condition
#define Deta_T 30.0

#define Gas_C 8.314  //气体常数

#define aerfa 3  //
#define gama 10000 
#define P0 1.013e5 //标准大气压
#define Bolt 5.67e-8 //
#define derta 0.002
#define derta2 0.0
#define dot 2.
#define k0 3.

//初始化
DEFINE_INIT(myinit,d)
{
	Thread **pt,*t;
	real l[ND_ND];
	real x,y,z;
	real time;
	cell_t c;
	time= RP_Get_Real("flow-time");
   
	mp_thread_loop_c(t,d,pt)
	if(FLUID_THREAD_P(t))
	{
		begin_c_loop(c,t)
		{
			Thread *pp = pt[0];
			Thread *sp = pt[1];
			Thread *tp = pt[2];
			C_CENTROID(l,c,t);
			x=l[0];
			y=l[1];
			z=l[2];
			if(z > H_air_down && z <= H_air_down + H)
			{
				C_VOF(c,pp)=1.0;
				C_VOF(c,sp)=0.0;
				C_VOF(c,tp)=0.0;
			}else
			{
				C_VOF(c,pp)=0.0;
				C_VOF(c,sp)=0.0;
				C_VOF(c,tp)=1.0;
			}
		}
		end_c_loop(c,t)
	}
}

// 限定速度，避免不收敛发生,主要是空气
DEFINE_ADJUST(velocity_adjust, domain)
{
	Thread *t, **pt;
	face_t f;
	cell_t c;
	mp_thread_loop_c(t, domain, pt)
		if (FLUID_THREAD_P(t))
		{
			Thread *pp = pt[0];
			Thread *sp = pt[1];
			Thread *tp = pt[2];

			begin_c_loop_int(c, t)
			{
				if (fabs(C_U(c, t))>7.0 || fabs(C_V(c, t))>7.0 || fabs(C_W(c, t))>7.0)
				{
					C_U(c, t) = C_U(c, t)*0.7;
					C_V(c, t) = C_V(c, t)*0.7;
					C_W(c, t) = C_W(c, t)*0.7;
				}
				else if (fabs(C_U(c, t))>6.0 || fabs(C_V(c, t))>6.0 || fabs(C_W(c, t))>6.0)
				{
					C_U(c, t) = C_U(c, t)*0.8;
					C_V(c, t) = C_V(c, t)*0.8;
					C_W(c, t) = C_W(c, t)*0.8;
				}
				else if (fabs(C_U(c, t))>5.0 || fabs(C_V(c, t))>5.0 || fabs(C_W(c, t))>5.0)
				{
					C_U(c, t) = C_U(c, t)*0.9;
					C_V(c, t) = C_V(c, t)*0.9;
					C_W(c, t) = C_W(c, t)*0.9;
				}
			}
			end_c_loop_int(c, t)
		}
}

//调整激光热源加载
DEFINE_ADJUST(laser_heat, domain)  //峰值线性递增-椎体热源
{
	cell_t c;
	Thread **pt, *t;
	real l[ND_ND], l_1[ND_ND];				//获得所有单元体总新坐标
	real x0, y0, z0, x1, y1, z1; 			//静动坐标变量
	real a, b, r0, r, Q0, Q_1, Q_2;
	real time;				//时间
	real source;
	real laser_efficiency;
	real ct, lat;
	time = RP_Get_Real("flow-time");

	real z_bottom, z_bottom_1; //小孔底部z坐标

	z_bottom = H_air_down + H;
	mp_thread_loop_c(t, domain, pt)
		if (FLUID_THREAD_P(t))
		{
			
			begin_c_loop(c, t)  //获取小孔深度
			{
				Thread *pp = pt[0];
				Thread *sp = pt[1];
				Thread *tp = pt[2];
				C_CENTROID(l_1, c, t);
				z_bottom_1 = l_1[2];
				if (C_VOF(c, pp) <= 0.5 && z_bottom_1 < z_bottom && z_bottom_1 >= H_air_down + 0.2e-3)
				{
					z_bottom = z_bottom_1;
				}
			}
			end_c_loop(c, t)

			if (H + H_air_down - z_bottom <= 0.1e-3)
			{
				z_bottom = H + H_air_down - 0.1e-3;
			}
			z_bottom = z_bottom - 0.2e-3;
			if (z_bottom < H_air_down)
			{
				z_bottom = H_air_down;
			}			

			Message("z_keyhole= %f ", z_bottom);

			begin_c_loop(c, t)
			{
				C_CENTROID(l, c, t);
				ct = C_T(c, t);
				x0 = l[0];				//获得体单元中心
				y0 = l[1];
				z0 = l[2];
				x1 = x0 - (v*time + L_start_point); //相对于热源中心的坐标
				y1 = y0;
				z1 = z0;

				a = (re - ri) / (ze - z_bottom);
				b = (ri*ze - re*z_bottom) / (ze - z_bottom);

				r0 = a*z1 + b;

				Q_1 = re - ri*Chi - (ri - re) / log(Chi)*(1 - Chi);
				Q_2 = re*re - ri*ri*Chi - 2 * (ri - re) / log(Chi)*Q_1;
				Q0 = 3 * Q * log(Chi) / (PI*(1 - pow(2.718, -3))*(z_bottom - ze)*Q_2);

				r = sqrt(pow(x1, 2.) + pow(y1, 2.));

				if (z1 <= H_air_down + H && z1 >= z_bottom && r <= r0)
				{
					C_UDMI(c, t, 1) = Q0*exp(log(Chi) / (z_bottom - ze)*(z1 - ze))*exp(-3 * r*r / (r0*r0));
				}
				else
				{
					C_UDMI(c, t, 1) = 0.0;
				}

				Thread *pp1 = pt[0];
				Thread *sp1 = pt[1];
				Thread *tp1 = pt[2];
				//辐射 对流 蒸发 散热
				if (C_VOF(c, pp1)>0.1 && C_VOF(c, pp1)<0.9)
				{
					//辐射
					C_UDMI(c, t, 5) = -Boerz*FI*(pow(ct, 4) - pow(temp0, 4)) / Grid_length;
					//对流
					C_UDMI(c, t, 6) = -hcov*(ct - temp0) / Grid_length;
					//蒸发 
					lat = EVAPORATION_LAT*molemass + (cof_h*(cof_h + 1.0) / 2.0 / (cof_h - 1.0))*8.314*ct;
					C_UDMI(c, t, 7) = -(0.82 * lat / sqrt(2 * PI*molemass*8.314*ct) * 101325 * exp(lat*(ct - T_SAT) / (8.314*ct*T_SAT))) / Grid_length;
				}
				else
				{
					C_UDMI(c, t, 5) = 0;
					C_UDMI(c, t, 6) = 0;
					C_UDMI(c, t, 7) = 0;
				}
			}
			end_c_loop(c, t)
		}
}

//定义能量源项
DEFINE_SOURCE(heat_source, c, t, dS, eqn)
{
	real source;
	source = C_UDMI(c, t, 1)+ C_UDMI(c, t, 5)+ C_UDMI(c, t, 6)+ C_UDMI(c, t, 7); //
	dS[eqn] = 0;
	return source;
}
//浮力
DEFINE_SOURCE(buoyancy, c, mix_th, dS, eqn)
{
	real pho, source;
	//密度 300-913  pho=-0.22*T+2745.9
	//913-926 pho=-10.9*T+12485.6
	//926-3000 pho=-0.37*T+2727.1
	if (C_T(c, mix_th) <= Ts || C_T(c, mix_th)>=T_SAT)
	{
		source = 0.0;
	}

	else if (C_T(c, mix_th)>Ts &&C_T(c, mix_th) < Tl)
	{
		pho = -3.86*C_T(c, mix_th)+ 5960;
		source = pho*g*beta*(C_T(c, mix_th) - Tl);
	}
	else if (C_T(c, mix_th) > Tl &&C_T(c, mix_th) < T_SAT)
	{
		pho = -0.37*C_T(c, mix_th) + 2732.7;
		source = pho*g*beta*(C_T(c, mix_th) - Tl);
	}
    dS[eqn] = 0.0;
	return source;
}

//表面张力
DEFINE_PROPERTY(surface_tension,c,t)
{
	real surface_tension;
	real T=C_T(c,t);
	if (T < Tl)
	{
		surface_tension = 0.8;  //0.8
	}
	else
	{
		surface_tension = 0.8 - 0.155e-3*(T - Tl); //参考书籍：金属及合金的表面张力  0.8 - 0.155e-3*(T - Tl)
	}
	return surface_tension;
}
//粘度
DEFINE_PROPERTY(viscosity, cell, t)
{
	real vis;
	real solid_f, solid_fcr;
	real ct = C_T(cell, t);
	solid_fcr = 0.68;


	if (ct >= Tl)
	{
		vis = L_Al_Vis;  
	}
	else if (ct < Ts)
	{
		vis = 1000.0;
	}
	else
	{
		vis = 1000.0 + (ct - Ts) / (Tl - Ts)*(L_Al_Vis - 1000.0);
	}
	//else
	//{
	//	solid_f = 1 - ((ct - Ts) / (Tl - Ts));
	//	vis = L_Al_Vis*pow(1.0 - (solid_f / solid_fcr), -1.55);
	//	//vis = min(vis,100.0);
	//	if (vis >= 100.0) vis = 100.0;
	//}
	return vis;
}
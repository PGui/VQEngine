//	DX11Renderer - VDemo | DirectX11 Renderer
//	Copyright(C) 2016  - Volkan Ilbeyli
//
//	This program is free software : you can redistribute it and / or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program.If not, see <http://www.gnu.org/licenses/>.
//
//	Contact: volkanilbeyli@gmail.com

#include "Quaternion.h"
#include <cmath>
#include <algorithm>	// min, max

#define DEG2RAD (XM_PI / 180.0f)
#define RAD2DEG (180.0f / XM_PI);

// private ctors : used by operator()s
Quaternion::Quaternion(float s, const XMFLOAT3& v)
	:
	S(s),
	V(v)
{}

Quaternion::Quaternion() : S(0), V() {}

// public ctors
Quaternion::Quaternion(float pitch, float yaw, float roll)
{
	// source: https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
	float t0 = std::cosf(yaw * 0.5f);
	float t1 = std::sinf(yaw * 0.5f);
	float t2 = std::cosf(roll * 0.5f);
	float t3 = std::sinf(roll * 0.5f);
	float t4 = std::cosf(pitch * 0.5f);
	float t5 = std::sinf(pitch * 0.5f);

	float w = t0 * t2 * t4 + t1 * t3 * t5;
	float x = t0 * t3 * t4 - t1 * t2 * t5;
	float y = t0 * t2 * t5 + t1 * t3 * t4;
	float z = t1 * t2 * t4 - t0 * t3 * t5;

	S = w;
	//V = XMFLOAT3(y, x, z);
	V = XMFLOAT3(y, z, x);	// i need an explanation why this works and not others:( 
							// read: euler vs yawpitchroll
	//V = XMFLOAT3(z, x, y);
}

// Pitch:	X
// Yaw:		Y
// Roll:	Z
Quaternion::Quaternion(const XMFLOAT3& pitchYawRoll)
	:
	Quaternion(pitchYawRoll.x, pitchYawRoll.y, pitchYawRoll.z)
{}

// Creates a quaternion from a rotation matrix
Quaternion::Quaternion(const XMMATRIX& rotMatrix)
{
	// THIS CODE BELOW WONT WORK:	directly taken from the PDFs, probably right-left hand issue,
	//								and I don't know how to fix it
	// TODO: figure this part out later
	//const XMMATRIX & m = rotMatrix;
	//S = 0.5f * sqrt(m.r[0].m128_f32[0] + m.r[1].m128_f32[1] + m.r[2].m128_f32[2] + 1);
	//V.x = (m.r[2].m128_f32[1] - m.r[1].m128_f32[2]) / (4.0f * S);
	//V.y = (m.r[0].m128_f32[2] - m.r[2].m128_f32[0]) / (4.0f * S);
	//V.z = (m.r[1].m128_f32[0] - m.r[0].m128_f32[1]) / (4.0f * S);

	// Therefore, I decided to use decompose function to get the XM quaternion, which doesn't work
	// with my system as well. I have to rotate it 180degrees on Y to get the correct rotation
	XMVECTOR scl = XMVectorZero();
	XMVECTOR quat = XMVectorZero();
	XMVECTOR trans = XMVectorZero();
	XMMatrixDecompose(&scl, &quat, &trans, XMMatrixTranspose(rotMatrix));
	//XMMatrixDecompose(&scl, &quat, &trans, rotMatrix);

	// hack zone
	//quat.m128_f32[2] *= -1.0f;
	
	//*this = Quaternion(quat.m128_f32[3], quat); //*Quaternion(0.0f, XM_PI, 0.0f);
	*this = Quaternion(quat.m128_f32[3], quat).Conjugate(); //*Quaternion(0.0f, XM_PI, 0.0f);
	int a = 5;
}

Quaternion::Quaternion(float s, const XMVECTOR & v)
	:
	S(s),
	V(v.m128_f32[0], v.m128_f32[1], v.m128_f32[2])
{}

Quaternion Quaternion::Identity()
{
	return Quaternion(1, XMFLOAT3(0.0f, 0.0f, 0.0f));
}


Quaternion Quaternion::FromAxisAngle(const XMVECTOR& axis, const float angle)
{
	const float half_angle = angle / 2;
	Quaternion Q = Quaternion::Identity();
	Q.S = cosf(half_angle);
	Q.V.x = axis.m128_f32[0] * sinf(half_angle);
	Q.V.y = axis.m128_f32[1] * sinf(half_angle);
	Q.V.z = axis.m128_f32[2] * sinf(half_angle);
	return Q;
}

Quaternion Quaternion::Lerp(const Quaternion & from, const Quaternion & to, float t)
{
	return  from * (1.0f - t) + to * t;
}

Quaternion Quaternion::Slerp(const Quaternion & from, const Quaternion & to, float t)
{
	double alpha	= std::acos(from.Dot(to));
	if (alpha < 0.00001) return from;
	double sina		= std::sin(alpha);
	double sinta	= std::sin(t * alpha);
	Quaternion interpolated = from * static_cast<float>(std::sin(alpha - t * alpha) / sina) + 
								to * static_cast<float>(sinta / sina);
	interpolated.Normalize();
	return interpolated;
}

DirectX::XMFLOAT3 Quaternion::ToEulerRad(const Quaternion& Q)
{
	// source: https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
	double ysqr = Q.V.y * Q.V.y;
	double t0 = -2.0f * (ysqr +  Q.V.z * Q.V.z) + 1.0f;
	double t1 = +2.0f * (Q.V.x * Q.V.y - Q.S * Q.V.z);
	double t2 = -2.0f * (Q.V.x * Q.V.z + Q.S * Q.V.y);
	double t3 = +2.0f * (Q.V.y * Q.V.z - Q.S * Q.V.x);
	double t4 = -2.0f * (Q.V.x * Q.V.x + ysqr) + 1.0f;

	t2 = t2 > 1.0f ? 1.0f : t2;
	t2 = t2 < -1.0f ? -1.0f : t2;

	float pitch = static_cast<float>(std::asin(t2));
	float roll  = static_cast<float>(std::atan2(t3, t4));
	float yaw   = static_cast<float>(std::atan2(t1, t0));
	return XMFLOAT3(roll, pitch, yaw);	// ??? (probably due to wiki convention)
}

DirectX::XMFLOAT3 Quaternion::ToEulerDeg(const Quaternion& Q)
{
	XMFLOAT3 eul = Quaternion::ToEulerRad(Q);
	eul.x *= RAD2DEG;
	eul.y *= RAD2DEG;
	eul.z *= RAD2DEG;
	return eul;
}


Quaternion Quaternion::operator+(const Quaternion & q) const
{
	Quaternion result;
	XMVECTOR V1 = XMVectorSet(  V.x,   V.y,   V.z, 0);
	XMVECTOR V2 = XMVectorSet(q.V.x, q.V.y, q.V.z, 0);

	result.S = this->S + q.S;
	XMStoreFloat3(&result.V, V1 + V2);
	return result;
}

Quaternion Quaternion::operator*(const Quaternion & q) const
{
	Quaternion result;
	XMVECTOR V1 = XMVectorSet(  V.x,   V.y,   V.z, 0);
	XMVECTOR V2 = XMVectorSet(q.V.x, q.V.y, q.V.z, 0);

	// s1s2 - v1.v2 
	result.S = this->S * q.S - XMVector3Dot(V1, V2).m128_f32[0];
	// s1v2 + s2v1 + v1xv2
	XMStoreFloat3(&result.V, this->S * V2 + q.S * V1 + XMVector3Cross(V1, V2));
	return result;
}

Quaternion Quaternion::operator*(float c) const
{
	Quaternion result;
	result.S = c*S;
	result.V = XMFLOAT3(V.x*c, V.y*c, V.z*c);
	return result;
}


bool Quaternion::operator==(const Quaternion& q) const
{
	double epsilons[4] = { 99999.0, 99999.0, 99999.0, 99999.0 };
	epsilons[0] = static_cast<double>(q.V.x) - static_cast<double>(this->V.x);
	epsilons[1] = static_cast<double>(q.V.y) - static_cast<double>(this->V.y);
	epsilons[2] = static_cast<double>(q.V.z) - static_cast<double>(this->V.z);
	epsilons[3] = static_cast<double>(q.S  ) - static_cast<double>(this->S  );
	bool same_x = std::abs(epsilons[0]) < 0.000001;
	bool same_y = std::abs(epsilons[1]) < 0.000001;
	bool same_z = std::abs(epsilons[2]) < 0.000001;
	bool same_w = std::abs(epsilons[3]) < 0.000001;
	return same_x && same_y && same_z && same_w;
}

// other operations
float Quaternion::Dot(const Quaternion & q) const
{
	XMVECTOR V1 = XMVectorSet(  V.x,   V.y,   V.z, 0);
	XMVECTOR V2 = XMVectorSet(q.V.x, q.V.y, q.V.z, 0);
	return std::max(-1.0f, std::min(S*q.S + XMVector3Dot(V1, V2).m128_f32[0], 1.0f));
}

float Quaternion::Len() const
{
	return sqrt(S*S + V.x*V.x + V.y*V.y + V.z*V.z);
}

Quaternion Quaternion::Inverse() const
{
	Quaternion result;
	float f = 1.0f / (S*S + V.x*V.x + V.y*V.y + V.z*V.z);
	result.S = f * S;
	result.V = XMFLOAT3(-V.x*f, -V.y*f, -V.z*f);
	return result;
}

Quaternion Quaternion::Conjugate() const
{
	Quaternion result;
	result.S = S;
	result.V = XMFLOAT3(-V.x, -V.y, -V.z);
	return result;
}

XMMATRIX Quaternion::Matrix() const
{
	XMMATRIX m = XMMatrixIdentity();
	float y2 = V.y * V.y;
	float z2 = V.z * V.z;
	float x2 = V.x * V.x;
	float xy = V.x * V.y;
	float sz = S * V.z;
	float xz = V.x * V.z;
	float sy = S * V.y;
	float yz = V.y * V.z;
	float sx = S * V.x;

	// -Z X -Y
	// LHS
	XMVECTOR r0 = XMVectorSet(1.0f - 2.0f * (y2 + z2), 2 * (xy - sz)		  , 2 * (xz + sy)		   , 0);
	XMVECTOR r1 = XMVectorSet(2 * (xy + sz)			 , 1.0f - 2.0f * (x2 + z2), 2 * (yz - sx)		   , 0);
	XMVECTOR r2 = XMVectorSet(2 * (xz - sy)			 , 2 * (yz + sx)		  , 1.0f - 2.0f * (x2 + y2), 0);
	XMVECTOR r3 = XMVectorSet(0						 , 0					  , 0					   , 1);

	//XMVECTOR r0 = XMVectorSet(2 * (xy - sz), -2.0f * (xz + sy), -(1.0f - 2.0f * (y2 + z2)), 0);
	//XMVECTOR r1 = XMVectorSet(-(1.0f - 2.0f * (x2 + z2)), 2.0f * (yz - sx), -2.0f * (xy + sz), 0);
	//XMVECTOR r2 = XMVectorSet(-2 * (yz + sx), -(1.0f - 2.0f * (x2 + y2)), 2.0f * (xz - sy), 0);
	//XMVECTOR r3 = XMVectorSet(0, 0, 0, 1);

	// RHS
	//XMVECTOR r0 = XMVectorSet(1.0f - 2.0f * (y2 + z2), 2 * (xy + sz)		  , 2 * (xz - sy)		   , 0);
	//XMVECTOR r1 = XMVectorSet(2 * (xy - sz)			 , 1.0f - 2.0f * (x2 + z2), 2 * (yz + sx)		   , 0);
	//XMVECTOR r2 = XMVectorSet(2 * (xz + sy)			 , 2 * (yz - sx)		  , 1.0f - 2.0f * (x2 + y2), 0);
	//XMVECTOR r3 = XMVectorSet(0						 , 0					  , 0					   , 1);

	m.r[0] = r0;
	m.r[1] = r1;
	m.r[2] = r2;
	m.r[3] = r3;
	return XMMatrixTranspose(m);
}

Quaternion& Quaternion::Normalize()
{
	float len = Len();
	if (len > 0.00001)
	{
		S = S / len;
		V.x = V.x / len;
		V.y = V.y / len;
		V.z = V.z / len;
	}
	return *this;
}

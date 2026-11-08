#pragma once
#include "core/Axis.h"
#include <cmath>

// XYZ를 double로 소유하는 계산용 값입니다. 축 접근과 연산 배치는 Boids와 통일합니다.
// PBD의 기존 수치 결과를 보존하도록 길이는 내적의 제곱근으로 계산합니다.
struct FVector3D
{
	double X = 0;

	double Y = 0;

	double Z = 0;

	double& operator[](EAxis Axis)
	{
		if (Axis == EAxis::X)
		{
			return X;
		}

		if (Axis == EAxis::Y)
		{
			return Y;
		}

		return Z;
	}

	const double& operator[](EAxis Axis) const
	{
		if (Axis == EAxis::X)
		{
			return X;
		}

		if (Axis == EAxis::Y)
		{
			return Y;
		}

		return Z;
	}

	FVector3D operator+(const FVector3D& Other) const
	{
		return {X + Other.X, Y + Other.Y, Z + Other.Z};
	}

	FVector3D operator-(const FVector3D& Other) const
	{
		return {X - Other.X, Y - Other.Y, Z - Other.Z};
	}

	FVector3D operator-() const
	{
		return {-X, -Y, -Z};
	}

	FVector3D operator*(double Scalar) const
	{
		return {X * Scalar, Y * Scalar, Z * Scalar};
	}

	FVector3D operator/(double Scalar) const
	{
		return {X / Scalar, Y / Scalar, Z / Scalar};
	}

	FVector3D& operator+=(const FVector3D& Other)
	{
		X += Other.X;
		Y += Other.Y;
		Z += Other.Z;

		return *this;
	}

	FVector3D& operator-=(const FVector3D& Other)
	{
		X -= Other.X;
		Y -= Other.Y;
		Z -= Other.Z;

		return *this;
	}

	FVector3D& operator*=(double Scalar)
	{
		X *= Scalar;
		Y *= Scalar;
		Z *= Scalar;

		return *this;
	}

	FVector3D& operator/=(double Scalar)
	{
		X /= Scalar;
		Y /= Scalar;
		Z /= Scalar;

		return *this;
	}

	bool operator==(const FVector3D& Other) const = default;

	/** 기존 PBD와 동일하게 X → Y → Z 내적을 더한 뒤 제곱근을 구합니다. */
	double Length() const
	{
		return std::sqrt(LengthSquared());
	}

	/** 제곱근 없이 길이의 제곱을 X → Y → Z 순서로 계산합니다. */
	double LengthSquared() const
	{
		return X * X + Y * Y + Z * Z;
	}

	/** 성분별 곱을 X → Y → Z 순서로 더해 내적을 계산합니다. */
	double Dot(const FVector3D& Other) const
	{
		return X * Other.X + Y * Other.Y + Z * Other.Z;
	}

	/** 입력 순서를 보존하여 오른손 좌표계의 외적을 계산합니다. */
	FVector3D Cross(const FVector3D& Other) const
	{
		return {Y * Other.Z - Z * Other.Y, Z * Other.X - X * Other.Z, X * Other.Y - Y * Other.X};
	}

	/** 모든 성분이 유한한지 확인하며 상태는 변경하지 않습니다. */
	bool IsFinite() const
	{
		return std::isfinite(X) && std::isfinite(Y) && std::isfinite(Z);
	}

	/** 세 성분에 같은 값을 저장합니다. 경계 초기화 시 무한대도 허용합니다. */
	void Fill(double Value)
	{
		X = Value;
		Y = Value;
		Z = Value;
	}
};

inline FVector3D operator*(double Scalar, const FVector3D& Vector)
{
	return {Scalar * Vector.X, Scalar * Vector.Y, Scalar * Vector.Z};
}

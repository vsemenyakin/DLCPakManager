#pragma once

namespace DLCPackageManagerPrivate
{
	template<typename HackingType, typename TypeToHack>
	const HackingType& GetHackedType(const TypeToHack& ValueToHack)
	{
		static_assert(sizeof(TypeToHack) == sizeof(HackingType), "Hacking type should contain same fields as TypeToHack");
		return reinterpret_cast<const HackingType&>(ValueToHack);
	}

	template<typename HackingType, typename TypeToHack>
	HackingType& GetHackedType(TypeToHack& ValueToHack)
	{
		const auto& ConstValueToHack = const_cast<const TypeToHack&>(ValueToHack);
		return const_cast<HackingType&>(GetHackedType<HackingType>(ConstValueToHack));
	}
}

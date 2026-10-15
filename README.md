# ✦ Starlight — mod menu do Geometry Dash

Mod na **Geode v5** dla **GD 2.2081 (Windows)**. Menu jest zrobione w ImGui (biblioteka `gd-imgui-cocos`), a wizuale w grze to proceduralne sprite'y Cocos2d (żadnych plików graficznych).

## Struktura

```
Starlight/
├─ mod.json, CMakeLists.txt, about.md
├─ .github/workflows/build.yml     ← automatyczny build na GitHubie
└─ src/
   ├─ main.cpp                     ← start moda, skróty klawiszowe
   ├─ Config.hpp/.cpp              ← wszystkie opcje + zapis/odczyt
   ├─ State.hpp                    ← statystyki (noclip, CPS, FPS)
   ├─ ui/Menu.cpp                  ← menu: motyw, animacje, panele, widgety
   ├─ hacks/Hacks.cpp              ← speedhack, FPS, bypassy, no mirror itd.
   ├─ hacks/PlayLayerHooks.cpp     ← noclip, safe mode, hitboxy, labelki, podpięcie wizuali
   └─ visuals/
      ├─ Textures.cpp              ← generowane tekstury (glow, księżyc, gradient)
      ├─ Skybox.cpp                ← niebo: noc / zachód / mgławica / krwawy księżyc
      └─ OrbTrail.cpp              ← orby lecące za graczem + aura
```

## Jak zbudować

### Opcja A — GitHub Actions (najprościej, nic nie instalujesz)
1. Załóż prywatne repo na GitHubie i wrzuć tam cały folder `Starlight`.
2. Zakładka **Actions** → build odpali się sam przy pushu.
3. Pobierz artefakt **Starlight** → w środku jest `piotrek.starlight.geode`.
4. Wrzuć plik `.geode` do `Geometry Dash/geode/mods/` i odpal grę.

### Opcja B — lokalnie
Potrzebne: Visual Studio 2022 (C++ desktop), CMake, Ninja, [Geode CLI](https://docs.geode-sdk.org/getting-started/geode-cli).
```
geode sdk install
geode sdk install-binaries
cd Starlight
geode build
```

## Sterowanie
- **Tab** — otwórz/zamknij menu
- Kliknij nagłówek panelu, żeby go zwinąć; przeciągnij, żeby przesunąć
- Strzałka `›` obok przełącznika = dodatkowe ustawienia
- *Interface → Reset Layout* przywraca domyślne ułożenie paneli

## Uwagi
- **Auto Safe Mode** jest domyślnie włączony: jak użyjesz noclipa/speedhacka/hitboxów w danej próbie, gra nie zapisze postępu ani ukończenia.
- FPS Bypass działa tylko z wyłączonym VSync w ustawieniach grafiki GD.
- To jest pierwsza wersja — nazwy pól z bindingów Geode mogą się różnić między wersjami, więc przy pierwszym buildzie mogą wyskoczyć błędy do poprawienia.

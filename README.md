# ProjectNR

Unreal Engine 5.8 3인칭 액션 프로토타입. **Game Animation Sample**(Mover 버전)을 바탕으로, 캐릭터 기능은
샘플 폰을 상속한 Blueprint(`BP_NRPlayer_V2`)에 **C++ ActorComponent**로 붙이는 구조다.

- 무기: 1번 라이플 / 2번 샷건(펠릿 12발) / 3번 근접(도끼 콤보)
- 구르기: `Left Ctrl`, 8방향, 근접 공격 캔슬
- 조준: 우클릭 홀드 = 견착, 클릭 = 정조준(1인칭)
- 무기 파츠: `Tab` 장착 화면, 무기별 전용 1 + 공용 5 슬롯, `R` 재장전
- 적 AI: 손으로 배선한 leaky 신경망 + StateTree (`NeuralAI` 모듈)

> **이 저장소에는 마켓·샘플 애셋이 없다.** 라이선스상 원본 애셋을 재배포할 수 없어서 `Content/`는
> 이 프로젝트에서 만든 애셋만 추적한다. 클론한 뒤 아래 팩을 같은 경로에 추가해야 열린다.

---

## 필요한 애셋 팩

| 경로 (`Content/`) | 내용 | 출처 |
|---|---|---|
| `Blueprints/`, `Characters/`, `Input/`, `Levels/`, `Audio/` 등 | 샘플 폰·ABP·무브먼트 모드·마네킹 | Game Animation Sample (Epic, UE 5.8) |
| `ThirdPerson/`, `LevelPrototyping/`, `Variant_*` | 맵·템플릿 | UE 5.8 Third Person 템플릿 (C++) |
| `Animations/ParagonAnimationsRetargetedToManny/` | TwinBlast `Dive_*` (구르기) | 파라곤 애니메이션, Manny 리타겟본 |
| `Animations/BelicaManny/` | 라이플 상체·에임 오프셋 | 파라곤 Belica → `SK_Mannequin` 리타겟 |
| `Animation/CombatMasterAnimBundle/` | 근접 콤보 `Anim_DA_Combo_*_RM` | Combat Master Anim Bundle (Fab) |
| `Animations/Rifle*`, `Pistol`, `Shotgun`, `HitReactions`, `Roll` | 기타 애니메이션 | 출처 확인 필요 |

## 샘플 애셋에 가한 수정 (다시 적용해야 함)

추적하지 않는 애셋이라 저장소에 들어 있지 않다.

| 애셋 | 수정 |
|---|---|
| `Blueprints/GM_Sandbox` | `PawnClasses_Soft = [BP_NRPlayer_V2, SandboxCharacter_Mover]`, `HUDClass = NRHUD` |
| `Blueprints/SandboxCharacter_Mover_ABP` | AnimGraph에 라이플 상체 레이어(`LayeredBoneBlend`, `spine_01` 이하, `neck_01` 제외) 추가, `HasWeapon`/`AimAlpha` 변수 |
| `Input/IMC_Sandbox` | `LeftControl → IA_Walk` 매핑 삭제 (Ctrl은 구르기) |
| `TwinBlastManny/Dive_*_Roll`, `CommboAttack/Anim_DA_Combo_A1/A2/A4_RM` | `bEnableRootMotion = true` |

플레이어 폰은 `DDCvar.PawnClass 0` (기본값 `-1`이면 `GM_Sandbox.DefaultPawnClass`가 스폰된다).

---

## 코드 구조

`Source/ProjectNR/`

| 파일 | 역할 |
|---|---|
| `NRWeaponComponent` | 무기 슬롯(원거리/근접), 화면 중앙 수렴 조준, 크로스헤어 상태 |
| `NRProjectiles` | 라이플 탄 / 샷건 펠릿 (사거리는 투사체 수명) |
| `NRHUD` | Canvas 크로스헤어·무기 이름·적 체력바·데미지 숫자 |
| `NRRollComponent` | 구르기. 이동은 전용 Mover 레이어드 무브 (진입 속도로 끝나는 곡선) |
| `NRMeleeComponent` | 근접 콤보. 루트 모션을 전용 레이어드 무브로 적용, 시작 시 A/D 방향 틀기 |
| `NRWeaponPartTypes` / `NRWeaponPartData` / `NRWeaponPartComponent` | 파츠 데이터, 장착 규칙, 런타임 스탯 (→ `docs/WEAPON_ATTACHMENTS.md`) |
| `NRLoadoutWidget` | C++로 구성한 임시 파츠 장착 화면 |
| `NRWeaponPartCommands` | 테스트 콘솔 `NR.Loadout`, `NR.Parts.*`, `NR.Weapon.*` |
| `NRAimComponent` | 견착 / 정조준. 우클릭 홀드·탭 판정, 1인칭 카메라, 샘플 `WantsToAim` 구동 |
| `NRMoveInput` | 샘플의 `Get_MoveInput`으로 실제 키 입력 방향 읽기 |
| `NRAnimInfoCommand` | 콘솔 `NR.AnimInfo` / `NR.AnimChain` — 루트 모션·스윙 구간·콤보 연결 자세 측정 |
| `NRPlayerPawn` | 이전 C++ 폰 (비교용) |

`Source/NeuralAI/` — 적 AI: 센서 → 신경망(25뉴런) → 컨트롤러, StateTree 노드, `UHealthComponent`.

## 빌드

UE 5.8, Visual Studio 2022. `ProjectNR.uproject` 우클릭 → Generate Visual Studio project files → `ProjectNREditor` 빌드.

참고: `Config/DefaultEngine.ini`의 `AndroidFileServer SecurityToken`은 엔진이 자동 생성한 개발용 값이다.

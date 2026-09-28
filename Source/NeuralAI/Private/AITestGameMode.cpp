// Neural Enemy AI prototype module.

#include "AITestGameMode.h"
#include "Characters/AITestPlayerCharacter.h"

AAITestGameMode::AAITestGameMode()
{
	DefaultPawnClass = AAITestPlayerCharacter::StaticClass();
}

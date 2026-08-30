# Changelog

## [1.2.0](https://github.com/Axertin/mth-apclient/compare/v1.1.0...v1.2.0) (2026-08-30)


### Features

* add option to randomize the Mirror's End switches ([#218](https://github.com/Axertin/mth-apclient/issues/218)) ([a06bb86](https://github.com/Axertin/mth-apclient/commit/a06bb861a22cf6c2565b870e9792ff20cc161749)), closes [#28](https://github.com/Axertin/mth-apclient/issues/28)
* Add traps, starting with a small set of vanilla modifiers ([#212](https://github.com/Axertin/mth-apclient/issues/212)) ([0a54337](https://github.com/Axertin/mth-apclient/commit/0a54337a1785caae93ba483b92d726f83de5d95f))
* attempt TLS backoff / upgrade on connection with no scheme defined ([c5d89c7](https://github.com/Axertin/mth-apclient/commit/c5d89c7e1dffa92d475a62525a70392a1100f425))
* enforce the AP safety gate by default ([72914fc](https://github.com/Axertin/mth-apclient/commit/72914fc69a46a5ecb634c15ea08186cf5bd4f138))
* scale the dev overlay to the display resolution ([7b17c1a](https://github.com/Axertin/mth-apclient/commit/7b17c1ad8c437c024ad89b0bb6426c9e54851064)), closes [#153](https://github.com/Axertin/mth-apclient/issues/153)


### Bug fixes

* deathlink no longer sends when you are saved by Proto Spark ([98bb812](https://github.com/Axertin/mth-apclient/commit/98bb8127229a240126e7aa0be7f85b0b0b6e6349))
* drop the spurious traps-inert warning at startup ([66d6b35](https://github.com/Axertin/mth-apclient/commit/66d6b35d7d506c67131aee4bfcfa9b4a04588484))
* publish the AP status and the staged-save flag across the thread boundary ([c6671b2](https://github.com/Axertin/mth-apclient/commit/c6671b29dc12954bcf9b464f968b7edc4234476f))
* report a refusal when a connect throws before the handshake ([ae72118](https://github.com/Axertin/mth-apclient/commit/ae72118edd99bde281ae2cabd89e825d0d42ec18))
* reworded max cap notation in pause menu to prevent line wrapping ([856630f](https://github.com/Axertin/mth-apclient/commit/856630f6951b5c648ec5d68ef82546ecb031ebdd)), closes [#220](https://github.com/Axertin/mth-apclient/issues/220)
* trim a newline out of a stored server or slot ([7207754](https://github.com/Axertin/mth-apclient/commit/7207754015b5c39046f0765c3fd1ceabd96d6471))


### Internals

* consolidate platform loggers into one ([#215](https://github.com/Axertin/mth-apclient/issues/215)) ([d977ed9](https://github.com/Axertin/mth-apclient/commit/d977ed9694943038c75ba486c9c9019817577bc6))
* drop the orphaned modifier CSV parser and a test-only wallet seam ([30826aa](https://github.com/Axertin/mth-apclient/commit/30826aa8fb6364803620f1c6c5dd3e61f098d783))
* drop the superseded save store and a dead coalescing branch ([04aec8a](https://github.com/Axertin/mth-apclient/commit/04aec8ac135b5e055bdc42e62ac90dce13886c90))
* generalize scene-walk operations into one helper ([#216](https://github.com/Axertin/mth-apclient/issues/216)) ([97e7054](https://github.com/Axertin/mth-apclient/commit/97e705431eaa1611303a0c36a9c912c603247100))
* move the takeover flush decision into the pure layer ([d855ff3](https://github.com/Axertin/mth-apclient/commit/d855ff331adf115fdb604ae8ae266cb7376ea503))
* pull duplicated PAL entries into shared files ([#214](https://github.com/Axertin/mth-apclient/issues/214)) ([1078da3](https://github.com/Axertin/mth-apclient/commit/1078da30c2bb9279430fe177dee8fc04e79a9c36))
* remove duplicate signature resolution logging ([b591d30](https://github.com/Axertin/mth-apclient/commit/b591d30ca6fd558be8b57cca7befc8295090cfcb))

## [1.1.0](https://github.com/Axertin/mth-apclient/compare/v1.0.0...v1.1.0) (2026-08-24)


### Features

* show the enforced stat cap in the bone-up menu ([6e55c64](https://github.com/Axertin/mth-apclient/commit/6e55c649b14ce148271a5fa17ae01b2a6f814755))
* show the enforced stat cap on the pause screen stat panels ([cc85173](https://github.com/Axertin/mth-apclient/commit/cc851739d6a1e238e086637f6112ccfadb88d535)), closes [#205](https://github.com/Axertin/mth-apclient/issues/205)


### Bug fixes

* align deathlink handling to the AP convention ([#203](https://github.com/Axertin/mth-apclient/issues/203)) ([c69623f](https://github.com/Axertin/mth-apclient/commit/c69623fb82d82622dea55e5c392726578ac09684))
* commit ap state with game save events rather than immediately ([6c8666c](https://github.com/Axertin/mth-apclient/commit/6c8666c54b3700c104d9a1c3e766e8163df8c321))
* guard against null AreaManager* in deathlink paths ([da745ab](https://github.com/Axertin/mth-apclient/commit/da745ab3f680c9333dfc5ed0179c9fddf2ed7db9))
* lock down only the modifiersAP is incompatible with ([a0eb539](https://github.com/Axertin/mth-apclient/commit/a0eb539f78f5c470715b19dee13d6f171b878167))
* notifications now display longer and five at a time ([dfa7dd9](https://github.com/Axertin/mth-apclient/commit/dfa7dd9c578b771c47d4192cd15b98ca6abc4f65))
* queue deathlinks properly when not in gameplay ([#202](https://github.com/Axertin/mth-apclient/issues/202)) ([ef28506](https://github.com/Axertin/mth-apclient/commit/ef285066c7cc979e576cd5ebaf44da23c1616767))
* swallow game input while bypassing save slot screen ([dd6fe34](https://github.com/Axertin/mth-apclient/commit/dd6fe34aaa71aab571b8f85f6a5212c7f8566767))
* windows crash handler now correctly reports NT protection faults ([c4f0ba7](https://github.com/Axertin/mth-apclient/commit/c4f0ba7ee9a9443374ae3b8b190ad20403c7d2ed))


### Build and CI

* change release job from actions-bot to custom app ([1ca912c](https://github.com/Axertin/mth-apclient/commit/1ca912c46f273bd7296a3c4f79b07dc2462e8d76))
* cut prereleases and stable releases in two release-please lanes ([#200](https://github.com/Axertin/mth-apclient/issues/200)) ([c9fc4e8](https://github.com/Axertin/mth-apclient/commit/c9fc4e8031fbba8535821b17d256133499954b98))
* cut releases with release-please ([c80b8e5](https://github.com/Axertin/mth-apclient/commit/c80b8e5afcc9848b2699b3240937c316d11e9b55))
* put the vcpkg manifest version under release-please ([80d1816](https://github.com/Axertin/mth-apclient/commit/80d1816774346ada7db9e006931ad6085c609ac9))
* ship debug info with release builds ([b5648b5](https://github.com/Axertin/mth-apclient/commit/b5648b52bd0f4e4cb4aa0cc3824c80e3a3b47efc))


### Documentation

* update faq ([333f423](https://github.com/Axertin/mth-apclient/commit/333f42366632aa801eb7a9d92e3a28f1b1d1706f))
* update FAQ with RTSS note and deathlink detail ([8c02b83](https://github.com/Axertin/mth-apclient/commit/8c02b83f707e5e0dd26f903bf94d7e30917be72a))
* update FAQ with start-in-ossex modifier info ([ecd9fe2](https://github.com/Axertin/mth-apclient/commit/ecd9fe242b9ce92cd1e3ced5d7bb858ef6577b75))

# gondohealth-utils

[English](README.md) | **Português**

Pequenos utilitários sem dependências para logística médica com drones, da
[**Gondohealth**](https://github.com/Gondohealth-org).

| ficheiro | o que é |
|---|---|
| [`geo.ts`](geo.ts) | Distância, rumo e comprimento de rota entre unidades de saúde, e verificação de pontos dentro de polígonos para áreas de serviço e zonas de exclusão aérea |
| [`flight-estimate.ts`](flight-estimate.ts) | Velocidade em relação ao solo corrigida pelo vento, tempo de voo e margem de bateria para um trajeto ou ida e volta |
| [`cold-chain.ts`](cold-chain.ts) | Detetar desvios de temperatura no registo do sensor da carga (ex.: vacinas a 2–8 °C) como prova de entrega |
| [`qgc-plan.ts`](qgc-plan.ts) | Criar uma missão `.plan` do QGroundControl (descolagem VTOL → pontos de passagem → aterragem) para PX4 / ArduPilot |
| [`contingency.ts`](contingency.ts) | Ponto de não retorno, decisões em voo (continuar / desviar / aterrar já) e rotas através de estações de carregamento ou de troca de baterias quando uma clínica está fora de alcance |
| [`solar.ts`](solar.ts) | Dimensionar estações solares: energia de carga diária, voos por dia, tempo de carga, potência dos painéis, armazenamento e baterias de reserva para estações de troca |
| [`station-panel.qml`](station-panel.qml) | Painel tátil Qt 6 / QML para uma estação de troca de baterias (sistema embarcado): energia solar, compartimentos de baterias, próxima chegada |
| [`sail_track.cpp`](sail_track.cpp) | Analisador de regatas em C++17 (só biblioteca padrão) para trilhos GPX / CSV: velocidade, VMG, perdas nas viragens, diagrama polar, eficiência por perna e CSV por segundo para dados de treino |

```ts
import { distanceM, bearingDeg } from "./geo"
import { estimateRoundTrip } from "./flight-estimate"

const quelimane = { lat: -17.8786, lng: 36.8883 }
const mocuba = { lat: -16.8392, lng: 36.9856 }

estimateRoundTrip(distanceM(quelimane, mocuba), bearingDeg(quelimane, mocuba), {
  cruiseSpeedMps: 25, cruisePowerW: 900, hoverPowerW: 3000, batteryWh: 4000,
}, { speedMps: 6, fromDeg: 0 })
// → { minutes: ~167, feasible: true, marginPct: ... }
```

```ts
import { planViaStations } from "./contingency"
import { panelWNeeded } from "./solar"

// Quelimane → Gurué (268 km) is out of range on 2.4 kWh, so hop via a solar station at Mocuba
planViaStations(quelimane, gurue, [mocuba], aircraft, { chargeW: 1000 })
// → { stops: [mocuba], minutes: ~256, stopMinutes: ~74 }   recharge
// with an autonomous battery swap at Mocuba ({ ...mocuba, swapMinutes: 3 }):
// → { stops: [mocuba], minutes: ~185, stopMinutes: 3 }

panelWNeeded(6, 1300, 5) // → 2312 W of panels for 6 recharges a day at 5 peak sun hours
```

```sh
g++ -std=c++17 -O2 -o sail_track sail_track.cpp
./sail_track race.gpx --wind 20 --mark -17.89,36.95 --mark -17.90,36.95 --svg polar.svg --csv samples.csv
```

As estimativas são apoios ao planeamento, não ferramentas de segurança de voo. Licença MIT.

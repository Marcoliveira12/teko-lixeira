# TekoLixeira

Lixeira inteligente com IoT para monitorar o descarte de resíduos.

<!-- IMAGEM DO PAINEL: salve a imagem em docs/ e troque este comentário por ![Painel do TekoLixeira](docs/NOME-DA-IMAGEM.png) -->

## O problema

Em Belém, a coleta de lixo segue rotas fixas, sem saber se a lixeira está cheia. Isso gasta tempo e combustível e não gera dados sobre o descarte.

## Como funciona

1. Uma célula de carga de feixe paralelo com o módulo HX711 mede o peso, e um sensor ultrassônico na tampa mede a distância até o lixo.
2. O ESP32 lê os sensores, filtra o ruído e envia os dados por Wi-Fi.
3. O Firebase Realtime Database guarda o histórico por dia e o estado atual da lixeira.
4. Este painel em React mostra tudo em tempo real.

## Funcionalidades do painel

- Gráfico do peso por hora do dia.
- Totais do mês atual e do mês anterior, com comparação entre os dois.
- Aviso de lixeira cheia ligado ao sensor, com a distância medida em centímetros.
- Mapa com a localização da lixeira.
- Tema claro e escuro.

## Tecnologias

**Software:** React 18 (Create React App), Firebase Realtime Database, ApexCharts, Leaflet com OpenStreetMap e Moment.js.

**Hardware:** ESP32, HX711, célula de carga, sensor ultrassônico e MPU6050.

## Origem e equipe

O TekoLixeira foi criado em conjunto por uma única equipe no Amazon Hacking 2025, programa de extensão do CESUPA, e foi exposto em um estande na COP 30, na Estação das Docas, em Belém.

**Engenharia da Computação**

- Igor Rafael Carvalho Gonçalves
- Marco Antônio Oliveira Machado
- Rafael Ponciano Vasconcelos Silva

**Ciência da Computação**

- Lucas Alves Rodrigues
- Raissa Sousa de Oliveira

**Orientação:** Profa. Luciana de Azevedo Vieira

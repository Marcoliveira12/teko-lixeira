# Teko Lixeira

Lixeira inteligente com IoT para monitorar o descarte de resíduos, apresentada na COP 30, em Belém.

![Tekinho, o mascote da Teko Lixeira, ao lado do logo](docs/teko-lixeira-apresentacao.png)

<!-- IMAGEM DO PAINEL: salve a imagem em docs/ e troque este comentário por ![Painel da Teko Lixeira](docs/NOME-DA-IMAGEM.png) -->

## O problema

Em Belém, os caminhões de coleta seguem rotas fixas, com dias e horários marcados, sem saber quanto lixo há em cada lixeira. Em um ponto, o caminhão chega e a lixeira está quase vazia: combustível, tempo e esforço perdidos. Ao mesmo tempo, outra lixeira está completamente cheia e fica sem coleta, porque a rota daquele dia não passa por ali. Empresas e órgãos públicos também não conseguem medir quanto resíduo produzem, e sem esses dados as decisões são tomadas no palpite.

## A solução

A Teko Lixeira é uma lixeira inteligente que monitora o peso e o nível do lixo e envia os dados por Wi-Fi para a nuvem. Um painel web mostra tudo em tempo real: o peso ao longo do dia, o total do mês e o status de lotação da lixeira. Com essas informações, a equipe de coleta sabe qual lixeira precisa ser esvaziada hoje e qual ainda pode esperar, e o caminhão vai direto aos pontos cheios, gastando menos combustível, tempo e esforço. O foco inicial são condomínios, prédios corporativos e órgãos públicos.

## Como funciona

1. Uma célula de carga de feixe paralelo com o módulo HX711 mede o peso, e um sensor ultrassônico na tampa mede a distância até o lixo.
2. O ESP32 lê os sensores, filtra o ruído e envia os dados por Wi-Fi.
3. O Firebase Realtime Database guarda o histórico por dia e o estado atual da lixeira.
4. Este painel em React mostra tudo em tempo real.

## Funcionalidades do painel

- Gráfico do peso por hora do dia.
- Totais do mês atual e do mês anterior, com comparação entre os dois.
- Status de lotação da lixeira ligado ao sensor ultrassônico, com a porcentagem de enchimento e o estado da bateria.
- Mapa com a localização da lixeira.
- Tema claro e escuro.

## Tecnologias

**Software:** React 18 (Create React App), Firebase Realtime Database, ApexCharts, Leaflet com OpenStreetMap e Moment.js.

**Hardware:** ESP32, HX711, célula de carga, sensor ultrassônico e MPU6050.

## Origem e equipe

A Teko Lixeira foi criada em conjunto por nossa equipe no Amazon Hacking 2025, programa de extensão do CESUPA, e foi exposta em um estande na COP 30, no Porto Futuro II, em Belém, onde apresentamos o projeto aos visitantes em um pitch. Depois, o projeto virou tema de dois TCCs: um de Engenharia da Computação, com o hardware, e um de Ciência da Computação, com o software.

**Engenharia da Computação**

- Igor Rafael Carvalho Gonçalves
- Marco Antônio Oliveira Machado
- Rafael Ponciano Vasconcelos Silva

**Ciência da Computação**

- Lucas Alves Rodrigues
- Raissa Sousa de Oliveira

**Orientação:** Profa. Luciana de Azevedo Vieira

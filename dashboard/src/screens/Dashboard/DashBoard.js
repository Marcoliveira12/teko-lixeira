import React, { useEffect, useState } from 'react';
import { ref, onValue } from 'firebase/database'; 
import { database } from '../../firebaseConfig'; 
import Map from '../../components/Map';
import moment from 'moment/moment';
import 'leaflet/dist/leaflet.css';
import NavbarDash from '../../components/NavbarDash';
import { lerPeso, somaColetadaNoDia, ehChaveDeData, acharUltimaLeitura, nomeBateria, ehErroDePermissao } from './dashBoardRequests';
import tekomente from '../../img/tekomente.png';
import LineChart from '../../components/Chart'; 
import { useTheme } from '../../theme';

function DashBoard() {
  const [firebaseHourlyData, setFirebaseHourlyData] = useState({}); // guarda o peso da lixeira por hora: { "HH:00": peso }
  const [, setTotalColetadoWeight] = useState("0.00"); // guarda o total efetivamente coletado (soma das diminuições de peso)
  const [loadingFirebaseData, setLoadingFirebaseData] = useState(true);
  const [firebaseError, setFirebaseError] = useState(null);
  const [statusLixeira, setStatusLixeira] = useState(null);
  const [totaisMensais, setTotaisMensais] = useState({ atual: 0, anterior: 0 });
  const [ultimaLeitura, setUltimaLeitura] = useState(null); // { nivel, epoch } da última medição com bateria
  const [semPermissao, setSemPermissao] = useState(false); // true quando as regras do Firebase bloqueiam a leitura
  const [mesReferencia, setMesReferencia] = useState(() => moment().format('YYYY-MM')); // mês corrente, 'YYYY-MM'

  // Confere a data a cada minuto: na virada do mês, troca o mês de referência
  // (isso recalcula os nomes dos meses e os totais de mês atual / mês anterior)
  useEffect(() => {
    const intervalo = setInterval(() => {
      const mesAgora = moment().format('YYYY-MM');
      setMesReferencia((mesAnterior) => (mesAnterior === mesAgora ? mesAnterior : mesAgora));
    }, 60 * 1000);
    return () => clearInterval(intervalo);
  }, []);

  const [position] = useState([-1.4558, -48.5039]);
  const [theme, toggleTheme] = useTheme();

  // --- UseEffect para coleta e consumo dos dados ---
  useEffect(() => {
    document.title = 'TekoLixeira';
    const today = moment();

        // Lê o dia de hoje, para acompanhar o hardware em tempo real
        const dataDeHoje = today.format('YYYY-MM-DD');
        console.log('Lendo do caminho:', 'lixeira/historico/' + dataDeHoje);
        const wasteRef = ref(database, 'lixeira/historico/' + dataDeHoje); 

    const unsubscribe = onValue(wasteRef, (snapshot) => {
      try {
        const timestampEntries = snapshot.val(); 
        const hourlyAggregatedData = {};

        // Inicializa todas as horas de 0 a 23 com 0 peso
        for (let i = 0; i < 24; i++) {
          const hourKey = String(i).padStart(2, '0') + ':00';
          hourlyAggregatedData[hourKey] = 0;
        }

        if (timestampEntries) {
          // Ordena as chaves cronologicamente pelo timestamp numérico
          const sortedTimestampKeys = Object.keys(timestampEntries).sort((a, b) => Number(a) - Number(b));
              
          sortedTimestampKeys.forEach(timestampKey => {
            const entry = timestampEntries[timestampKey]; // Objeto { hora: ..., peso: ... }

            if (entry && entry.hora !== undefined && lerPeso(entry) !== undefined) {
              const horaString = entry.hora;
              const pesoAtual = lerPeso(entry);

              const hourOnly = parseInt(horaString.split(':')[0], 10);

              if (!isNaN(hourOnly) && hourOnly >= 0 && hourOnly <= 23 && !isNaN(pesoAtual)) {
                const formattedHourKey = String(hourOnly).padStart(2, '0') + ':00';

                // Para o gráfico de peso da lixeira por hora: mantemos o peso registrado no momento
                hourlyAggregatedData[formattedHourKey] = pesoAtual;
              } else {
                console.warn(`Erro de formatação; Dados: ${timestampKey}: ${horaString}`);
              }
            }
          });
        }

        const finalChartData = {};
        for (let i = 0; i < 24; i++) {
          const hourKey = String(i).padStart(2, '0') + ':00';
          finalChartData[hourKey] = parseFloat((hourlyAggregatedData[hourKey] || 0).toFixed(2));
        }

        setFirebaseHourlyData(finalChartData);
        setTotalColetadoWeight(somaColetadaNoDia(timestampEntries).toFixed(2));
        setLoadingFirebaseData(false);

      } catch (err) {
        console.error("Erro na coleta de dados:", err);
        setFirebaseError("Erro no processamento de dados.");
        setLoadingFirebaseData(false);
      }
    }, (err) => {
      console.error("Erro na conexão ao firebase:", err);
      if (ehErroDePermissao(err)) {
        setSemPermissao(true);
      }
      setFirebaseError("Não conseguimos se conectar ao firebase.");
      setLoadingFirebaseData(false);
    });

    return () => unsubscribe();
  }, []);

  useEffect(() => {
    const statusRef = ref(database, 'lixeira/status');
    const unsubscribe = onValue(statusRef, (snapshot) => {
      setStatusLixeira(snapshot.val());
    }, (err) => {
      console.error("Erro ao ler o status da lixeira:", err);
      if (ehErroDePermissao(err)) {
        setSemPermissao(true);
      }
      setStatusLixeira(null);
    });
    return () => unsubscribe();
  }, []);

  useEffect(() => {
    const historicoRef = ref(database, 'lixeira/historico');
    const mesAtual = mesReferencia;
    const mesAnterior = moment(mesReferencia, 'YYYY-MM').subtract(1, 'month').format('YYYY-MM');
    const unsubscribe = onValue(historicoRef, (snapshot) => {
      const dias = snapshot.val() || {};
      let atual = 0;
      let anterior = 0;
      Object.keys(dias).forEach((chaveDia) => {
        if (!ehChaveDeData(chaveDia)) return;
        const totalDoDia = somaColetadaNoDia(dias[chaveDia]);
        if (chaveDia.startsWith(mesAtual)) atual += totalDoDia;
        else if (chaveDia.startsWith(mesAnterior)) anterior += totalDoDia;
      });
      setTotaisMensais({ atual, anterior });
      // Bateria: pega a medição mais recente do historico
      setUltimaLeitura(acharUltimaLeitura(dias));
    }, (err) => {
      console.error("Erro ao ler o historico mensal:", err);
      if (ehErroDePermissao(err)) {
        setSemPermissao(true);
      }
    });
    return () => unsubscribe();
  }, [mesReferencia]);

  const lastMonthWeight = totaisMensais.anterior;
  const thisMonthWeight = totaisMensais.atual;

  // Exibição dos pesos: vírgula decimal e espaço antes da unidade (ex.: "557,90 kg")
  const formatarKg = (valor) => valor.toFixed(2).replace('.', ',') + ' kg';
  const nomeDoMes = (data) => {
    const nome = data.toLocaleDateString('pt-BR', { month: 'long' });
    return nome.charAt(0).toUpperCase() + nome.slice(1);
  };
  const inicioMesAtual = moment(mesReferencia, 'YYYY-MM').toDate();
  const nomeMesAtual = nomeDoMes(inicioMesAtual);
  const nomeMesAnterior = nomeDoMes(new Date(inicioMesAtual.getFullYear(), inicioMesAtual.getMonth() - 1, 1));
  const diferencaMensal = thisMonthWeight - lastMonthWeight;
  // Sinal real: − quando diminuiu, + quando aumentou, sem sinal quando igual ("0,00 kg")
  let textoDiferenca = formatarKg(Math.abs(diferencaMensal));
  if (Number(diferencaMensal.toFixed(2)) > 0) {
    textoDiferenca = '+' + textoDiferenca;
  } else if (Number(diferencaMensal.toFixed(2)) < 0) {
    textoDiferenca = '−' + textoDiferenca;
  }

  const lixeiraCheia = statusLixeira ? statusLixeira.cheia === true : false;
  const temStatus = statusLixeira !== null && statusLixeira !== undefined;

  // Nível de enchimento medido pelo sonar (0 a 100)
  let percentualCheia = null;
  if (temStatus && statusLixeira.percentualCheia !== undefined && statusLixeira.percentualCheia !== null) {
    const percentualLido = parseInt(statusLixeira.percentualCheia, 10);
    if (!isNaN(percentualLido)) {
      percentualCheia = Math.min(100, Math.max(0, percentualLido));
    }
  }

  // Cor da barra pelo nível: verde até 59%, amarelo de 60% a 84%, vermelho de 85% para cima
  let classeNivel = 'isOk';
  if (percentualCheia !== null && percentualCheia >= 85) {
    classeNivel = 'isFull';
  } else if (percentualCheia !== null && percentualCheia >= 60) {
    classeNivel = 'isWarn';
  }

  // Manchete pelo nível, com a mesma cor da barra
  let textoAviso;
  let classeManchete;
  if (lixeiraCheia || (percentualCheia !== null && percentualCheia >= 85)) {
    textoAviso = '⚠ Coleta necessária';
    classeManchete = 'isFull';
  } else if (percentualCheia === null) {
    textoAviso = 'Sem leitura da lixeira';
    classeManchete = 'isEmpty';
  } else if (percentualCheia >= 60) {
    textoAviso = 'Coleta em breve';
    classeManchete = 'isWarn';
  } else {
    textoAviso = 'Sem necessidade de coleta';
    classeManchete = 'isOk';
  }

  // Bateria da última medição
  let textoBateria = 'sem leitura';
  let textoUltimaMedicao = 'Última medição: sem leitura';
  let segmentosBateria = 0;
  let classeBateria = 'isEmpty';
  if (ultimaLeitura !== null) {
    const nomeLido = nomeBateria(ultimaLeitura.nivel);
    textoUltimaMedicao = 'Última medição: ' + moment.unix(ultimaLeitura.epoch).format('DD/MM [às] HH:mm');
    if (nomeLido !== 'Sem leitura') {
      textoBateria = nomeLido;
    }
    if (nomeLido === 'Excelente') {
      segmentosBateria = 4;
      classeBateria = 'isOk';
    } else if (nomeLido === 'Boa') {
      segmentosBateria = 3;
      classeBateria = 'isOk';
    } else if (nomeLido === 'Baixa') {
      segmentosBateria = 2;
      classeBateria = 'isWarn';
    } else if (nomeLido === 'Carregar') {
      segmentosBateria = 1;
      classeBateria = 'isFull';
    }
  }

  return (
    <div className='main'>
      <NavbarDash theme={theme} onToggleTheme={toggleTheme} />
      <div className='selectContainer'>
        <select>
          <option value={'lixeira'}>Trocar lixeira</option>
          <option value={'lixeira2'}>Lixeira Cesupa</option>
        </select>
      </div>
      <div className="DashBoard">
        <div className='outerMapContainer'>
          <div className='chartanddash'>
            <div className='chart'>
              <h1 className='sectionTitle'>Status da lixeira</h1>
              <div className={semPermissao || classeManchete === 'isFull' ? 'trashWarningBar isFull' : 'trashWarningBar'}>
                {semPermissao ? (
                  <h1 className='warningText isFull'>Sem permissão para ler o Firebase. Confira as regras do banco.</h1>
                ) : (
                  <h1 className={'warningText ' + classeManchete}>{textoAviso}</h1>
                )}
              </div>

              <div className='monthCardsContainer'>
                <div className='monthCard'>
                  <div className='monthCardBody'>
                    <h1 className='monthCardTitle'>
                      {percentualCheia !== null ? percentualCheia + '%' : 'sem leitura'}
                    </h1>
                    <div className='trashWarningIndicator'>
                      <div
                        className='trashWarningPercentTrack'
                        role='progressbar'
                        aria-label='Nível de enchimento'
                        aria-valuenow={percentualCheia !== null ? percentualCheia : 0}
                        aria-valuemin={0}
                        aria-valuemax={100}
                      >
                        <div
                          className={'trashWarningPercentFill ' + classeNivel}
                          style={{ width: (percentualCheia !== null ? percentualCheia : 0) + '%' }}
                        />
                      </div>
                    </div>
                  </div>
                  <h2 className='monthCardLabel'>Enchimento</h2>
                </div>
                <div className='monthCard'>
                  <div className='monthCardBody'>
                    <h1 className={'monthCardTitle trashWarningBatteryLevel ' + classeBateria}>{textoBateria}</h1>
                    <div className='trashWarningIndicator'>
                      <svg
                        className={'trashWarningBatteryIcon ' + classeBateria}
                        viewBox='0 0 64 32'
                        width='64'
                        height='32'
                        aria-hidden='true'
                        focusable='false'
                      >
                        <rect className='trashWarningBatteryBody' x='1.5' y='1.5' width='55' height='29' rx='5' />
                        <rect className='trashWarningBatteryCap' x='58' y='10' width='5' height='12' rx='2' />
                        {[0, 1, 2, 3].map((indice) => (
                          <rect
                            key={indice}
                            className={indice < segmentosBateria ? 'trashWarningBatterySegment isOn' : 'trashWarningBatterySegment'}
                            x={6 + indice * 12.5}
                            y='6'
                            width='10'
                            height='20'
                            rx='2'
                          />
                        ))}
                      </svg>
                    </div>
                  </div>
                  <h2 className='monthCardLabel'>Bateria</h2>
                </div>
              </div>
              {/* Sempre presente, para o espaçamento até a próxima seção não mudar sem leitura */}
              <p className='trashWarningCaption'>{textoUltimaMedicao}</p>

              <h1 className='sectionTitle'>Peso coletado</h1>
              <div className='monthCardsContainer sectionBlock'>
                <div className='monthCard'>
                  <div className='monthCardBody'>
                    <h1 className='monthCardTitle'>{formatarKg(lastMonthWeight)}</h1>
                  </div>
                  <h2 className='monthCardLabel'>{nomeMesAnterior}</h2>
                </div>
                <div className='monthCard'>
                  <div className='monthCardBody'>
                    <h1 className='monthCardTitle'>{formatarKg(thisMonthWeight)}</h1>
                  </div>
                  <h2 className='monthCardLabel'>{nomeMesAtual}</h2>
                </div>
                <div className='monthCard'>
                  <div className='monthCardBody'>
                    <h1 className='monthCardTitle'>{textoDiferenca}</h1>
                  </div>
                  <h2 className='monthCardLabel'>Comparativo mensal</h2>
                </div>
              </div>

              <h1 className='sectionTitle'>Peso por hora</h1>
              <div className="outerChartContainer sectionBlock">
                <div className="chartContainer">
                  {loadingFirebaseData ? (
                    <div>Carregando dados do gráfico...</div>
                  ) : firebaseError ? (
                    <div>Erro ao carregar gráfico: {firebaseError}</div>
                  ) : Object.keys(firebaseHourlyData).length === 0 ? (
                    <div>Nenhum dado de peso disponível para o gráfico.</div>
                  ) : (
                    <LineChart data={firebaseHourlyData} theme={theme} />
                  )}
                </div>
              </div>
            </div>

            <div className='mapAndSideWrapper'>
              <div className='chartanddash mapLayout'>
                <div>
                  <h1>Localização da Lixeira</h1>
                  <div className="mapContainer">
                    <Map position={position} />
                  </div>  
                </div>
                <div className='mapSideContent'>
                  <div className='imageContainer'>
                    <img src={tekomente} alt="TekoMente Logo" className='tekoimg' />
                  </div>
                </div>
              </div>
            </div>
          </div>  
        </div>
      </div>
    </div>
  );
}

export default DashBoard;
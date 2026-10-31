import React, { useEffect, useState } from 'react';
import { ref, onValue } from 'firebase/database'; 
import { database } from '../../firebaseConfig'; 
import Map from '../../components/Map';
import moment from 'moment/moment';
import 'leaflet/dist/leaflet.css';
import NavbarDash from '../../components/NavbarDash';
import { isThereMore, lerPeso, somaColetadaNoDia, ehChaveDeData } from './dashBoardRequests';
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
      setStatusLixeira(null);
    });
    return () => unsubscribe();
  }, []);

  useEffect(() => {
    const historicoRef = ref(database, 'lixeira/historico');
    const mesAtual = moment().format('YYYY-MM');
    const mesAnterior = moment().subtract(1, 'month').format('YYYY-MM');
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
    }, (err) => {
      console.error("Erro ao ler o historico mensal:", err);
    });
    return () => unsubscribe();
  }, []);

  const lastMonthWeight = totaisMensais.anterior;
  const thisMonthWeight = totaisMensais.atual;

  const lixeiraCheia = statusLixeira ? statusLixeira.cheia === true : false;
  const temStatus = statusLixeira !== null && statusLixeira !== undefined;
  let textoAviso;
  if (!temStatus) {
    textoAviso = 'Sem leitura de status da lixeira';
  } else if (lixeiraCheia) {
    textoAviso = '⚠ A lixeira está cheia';
  } else {
    textoAviso = '⚠ A lixeira não está cheia';
  }
  const distanciaCm = temStatus && statusLixeira.distanciaCm !== undefined
    ? parseFloat(statusLixeira.distanciaCm)
    : null;

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
              <div className={lixeiraCheia ? 'trashWarningBar isFull' : 'trashWarningBar'}>
                <h1 className={lixeiraCheia ? 'warningText isFull' : 'warningText'}>{textoAviso}</h1>
                {distanciaCm !== null && (
                  <p className='warningSubText'>
                    Espaço livre medido pelo sensor: {distanciaCm.toFixed(1)} cm
                  </p>
                )}
              </div>

              <div className='monthCardsContainer'>
                <div className='monthCard'>
                  <h1 className='monthCardTitle'>{lastMonthWeight.toFixed(2)}kg</h1>
                  <h2>Mês passado</h2>
                </div>
                <div className='monthCard'>
                  <h1 className='monthCardTitle'>{thisMonthWeight.toFixed(2)}kg</h1>
                  <h2>Esse mês</h2>
                </div>
                <div className='monthCard'>
                  <h1>{isThereMore(thisMonthWeight, lastMonthWeight)}</h1>
                </div>
              </div>

              <h1>Dados de coleta</h1>
              <div className='selectContainer'>
              </div>
              <div className="outerChartContainer">
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
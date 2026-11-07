export const LIMIAR_COLETA = 0.1;

export function isThereMore(currentWeight, previousWeight) {
  const current = parseFloat(currentWeight) || 0;
  const previous = parseFloat(previousWeight) || 0;
  const diff = current - previous;
  const absDiff = Math.abs(diff).toFixed(2);

  if (previous === 0) {
    return `Sem dados do mês passado para comparar`;
  }
  if (diff > 0) {
    return `Foram coletados ${absDiff}kg a mais que mês passado`;
  } else if (diff < 0) {
    return `Foram coletados ${absDiff}kg a menos que mês passado`;
  } else {
    return `Foi coletada a mesma quantidade de peso que mês passado`;
  }
}

export function lerPeso(entrada) {
  if (!entrada) return undefined;
  const bruto = entrada.peso !== undefined ? entrada.peso : entrada.pesoTotal;
  if (bruto === undefined) return undefined;
  const numero = parseFloat(bruto);
  return isNaN(numero) ? undefined : numero;
}

export function somaColetadaNoDia(entradasDoDia) {
  if (!entradasDoDia) return 0;
  const chavesOrdenadas = Object.keys(entradasDoDia).sort((a, b) => Number(a) - Number(b));
  let total = 0;
  let pesoAnterior = null;
  chavesOrdenadas.forEach((chave) => {
    const peso = lerPeso(entradasDoDia[chave]);
    if (peso === undefined) return;
    if (pesoAnterior !== null && peso < pesoAnterior) {
      const queda = pesoAnterior - peso;
      if (queda >= LIMIAR_COLETA) total += queda;
    }
    pesoAnterior = peso;
  });
  return total;
}

export function ehChaveDeData(chave) {
  return /^\d{4}-\d{2}-\d{2}$/.test(chave);
}

// --- Bateria ---

// Procura a medição mais recente que tenha bateriaNivel, em todos os dias do historico
export function acharUltimaLeitura(dias) {
  let ultima = null;
  if (dias && typeof dias === 'object') {
    const chavesDias = Object.keys(dias);
    for (let i = 0; i < chavesDias.length; i++) {
      const chaveDia = chavesDias[i];
      const entradasDoDia = dias[chaveDia];
      if (ehChaveDeData(chaveDia) && entradasDoDia && typeof entradasDoDia === 'object') {
        const chavesEpoch = Object.keys(entradasDoDia);
        for (let j = 0; j < chavesEpoch.length; j++) {
          const epoch = Number(chavesEpoch[j]);
          const entrada = entradasDoDia[chavesEpoch[j]];
          if (!isNaN(epoch) && entrada && entrada.bateriaNivel !== undefined && entrada.bateriaNivel !== null) {
            if (ultima === null || epoch > ultima.epoch) {
              ultima = { nivel: entrada.bateriaNivel, epoch: epoch };
            }
          }
        }
      }
    }
  }
  return ultima;
}

// Traduz o nível que o ESP manda para o texto do painel
export function nomeBateria(nivel) {
  let nome = 'Sem leitura';
  if (typeof nivel === 'string') {
    const nivelMinusculo = nivel.toLowerCase();
    if (nivelMinusculo === 'excelente') {
      nome = 'Excelente';
    } else if (nivelMinusculo === 'bom') {
      nome = 'Boa';
    } else if (nivelMinusculo === 'baixa') {
      nome = 'Baixa';
    } else if (nivelMinusculo === 'carregar') {
      nome = 'Carregar';
    }
  }
  return nome;
}

// --- Permissão do Firebase ---

// Confere se o Firebase recusou a leitura por causa das regras do banco
export function ehErroDePermissao(err) {
  let semPermissao = false;
  if (err) {
    if (err.code === 'PERMISSION_DENIED') {
      semPermissao = true;
    }
    if (typeof err.message === 'string' && err.message.toLowerCase().indexOf('permission_denied') !== -1) {
      semPermissao = true;
    }
  }
  return semPermissao;
}

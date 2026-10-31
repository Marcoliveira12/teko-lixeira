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

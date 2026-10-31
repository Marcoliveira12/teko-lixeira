// src/components/LineChart.jsx
import React from 'react';
import Chart from 'react-apexcharts';

function LineChart({ data, theme = 'light' }) {
  const categories = Object.keys(data); // e.g., ["00:00", "01:00", ...]
  const values = Object.values(data);   // e.g., [0, 0.32, 0, ...]

  const isDark = theme === 'dark';

  const chartOptions = {
    theme: {
      mode: isDark ? 'dark' : 'light'
    },
    chart: {
      id: 'line-chart',
      type: 'line',
      background: 'transparent',
      // You might want to consider setting animations.enabled to false if you notice flickering
      // after a data update, although it can make transitions less smooth.
      // animations: {
      //   enabled: false,
      // }
    },
    markers: {
      size: 6,
      colors: ['#2C7A7B'],
      strokeWidth: 2,
      strokeColors: isDark ? '#1A2420' : '#fff',
      hover: {
        size: 8
      }
    },
    xaxis: {
      categories: categories,
      title: {
        text: 'Hora do Dia' // Added a title for clarity
      },
      tickAmount: 24, // Ensures all 24 ticks are shown if space allows
      labels: {
        rotate: -45, // Rotate labels to prevent overlap
        rotateAlways: true,
        formatter: function (val) {
          // Optional: format X-axis labels if needed, e.g., only show every 2 hours
          // if (parseInt(val.split(':')[0]) % 2 === 0) {
          //   return val;
          // }
          // return '';
          return val; // Display "HH:00" as is
        }
      }
    },
    yaxis: {
        title: {
            text: 'Peso (kg)' // Added a title for clarity
        }
    },
    stroke: {
      curve: 'smooth',
      colors: ['#0A8754']
    },
    title: {
      text: 'Peso do Lixo por Hora', // Updated title
      align: 'left'
    },
    tooltip: {
      enabled: true, // Ensure tooltips are enabled
      shared: false, // Important: Set to false for better control with discrete X-axis
      intersect: true, // Important: Tooltip shows only when hovering directly over a data point/marker
      // Alternatively, try shared: true and intersect: false for a wider hover area
      // shared: true,
      // intersect: false,
      x: {
        show: true, // Show the X-axis label in the tooltip
        formatter: function (val) {
          return `Hora: ${val}`; // Customize X-axis label in tooltip
        }
      },
      y: {
        formatter: function (val) {
          return `${val !== null ? val.toFixed(2) : 'N/A'} kg`; // Customize Y-axis label in tooltip
        }
      },
      // You can also add a fixed position if you want the tooltip to always appear in the same spot
      // fixed: {
      //   enabled: true,
      //   position: 'topRight', // topRight, topLeft, bottomRight, bottomLeft
      //   offsetX: 0,
      //   offsetY: 0,
      // },
      // To keep tooltip visible for a longer duration after mouse out
      // On the official docs, I don't see a direct `hideDelay` option in `tooltip` but in `markers`.
      // The best workaround is often `shared: false` with `intersect: true` or `shared: true` with `intersect: false`.
    }
  };

  const chartSeries = [
    {
      name: 'Peso (kg)',
      data: values,
    }
  ];

  return <Chart options={chartOptions} series={chartSeries} type="line" height={350} />;
}

export default LineChart;
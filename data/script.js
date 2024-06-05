let socket;
let chart;
let dataPoints = [];

function deleteDataLogFile() {
    socket.send(JSON.stringify({ request: "deleteDataLogFile" }));
    dataPoints = [];
    updateChart();
}

function downloadDataLogFile() {
    fetch('/download')
        .then(response => response.blob())
        .then(blob => {
            let url = window.URL.createObjectURL(blob);
            let a = document.createElement('a');
            a.href = url;
            a.download = 'dataLog.json';
            document.body.appendChild(a);
            a.click();
            a.remove();
        })
        .catch(error => console.error('Error downloading file:', error));
}

function enterConfigMode() {
    fetch('/configMode', { method: 'POST' })
        .then(response => {
            if (response.ok) {
                alert('Entering configuration mode. Please reconnect to the new network.');
            } else {
                alert('Failed to enter configuration mode.');
            }
        })
        .catch(error => console.error('Error entering configuration mode:', error));
}

document.addEventListener("DOMContentLoaded", function () {
    socket = new WebSocket(`ws://${window.location.hostname}/ws`);

    socket.onopen = function () {
        console.log("WebSocket connection established");
        socket.send(JSON.stringify({ request: "wholeLog" }));
    };

    document.getElementById("deleteBtn").addEventListener("click", function () {
        deleteDataLogFile();
    });

    document.getElementById("downloadBtn").addEventListener("click", function () {
        downloadDataLogFile();
    });

    document.getElementById("configBtn").addEventListener("click", function () {
        enterConfigMode();
    });

    socket.onmessage = function (event) {
        let data = JSON.parse(event.data);
        // console.log("Data received: ", data);
        if (data.log && Array.isArray(data.log)) {
            dataPoints = data.log;
        } else {
            dataPoints.push(data);
        }
    };

    socket.onclose = function () {
        console.log("WebSocket connection closed");
    };

    chart = Highcharts.chart('container', {
      chart: {
          type: 'gauge',
          plotBackgroundColor: null,
          plotBackgroundImage: null,
          plotBorderWidth: 0,
          plotShadow: false,
          height: '80%'
      },
      title: {
          text: 'Energy Collector Data'
      },
      pane: {
          startAngle: -90,
          endAngle: 89.9,
          background: null,
          center: ['50%', '75%'],
          size: '110%'
      },
      yAxis: {
          min: 0,
          max: 90,
          tickPixelInterval: 72,
          tickPosition: 'inside',
          tickColor: Highcharts.defaultOptions.chart.backgroundColor || '#FFFFFF',
          tickLength: 20,
          tickWidth: 2,
          minorTickInterval: null,
          labels: {
              distance: 20,
              style: {
                  fontSize: '14px'
              }
          },
          lineWidth: 0,
          plotBands: [{
              from: 0,
              to: 30,
              color: '#55BF3B',
              thickness: 20,
              borderRadius: '50%'
          }, {
              from: 30,
              to: 60,
              color: '#DDDF0D',
              thickness: 20,
              borderRadius: '50%'
          }, {
              from: 60,
              to: 90,
              color: '#DF5353',
              thickness: 20
          }]
      },
      series: [{
          name: 'Kwh',
          data: [0],
          dataLabels: {
              format: '<div style="text-align:center"><span style="font-size:25px">{y}</span><br/><span style="font-size:12px;opacity:0.4">Kwh</span></div>',
              borderWidth: 0,
              color: (
                  Highcharts.defaultOptions.title &&
                  Highcharts.defaultOptions.title.style &&
                  Highcharts.defaultOptions.title.style.color
              ) || '#333333',
              style: {
                  fontSize: '16px'
              }
          },
          dial: {
              radius: '80%',
              backgroundColor: 'gray',
              baseWidth: 12,
              baseLength: '0%',
              rearLength: '0%'
          },
          pivot: {
              backgroundColor: 'gray',
              radius: 6
          }
      }]
  });

    setInterval(updateChart, 5000); // Update the chart every 5 seconds
});

function updateChart() {
    // Calculate the impulses per hour
    let impulsesPerHour = calculateImpulsesPerHour();
    // Update the chart with the calculated impulses per hour
    chart.series[0].setData([impulsesPerHour]);
}

function calculateImpulsesPerHour() {
    if (dataPoints.length === 0) {
    return 0;
    }

    // find impulses per hour
    // we have datapoints.time which is unixtimestamp


    // get the last timestamp
    let lastTimestamp = dataPoints[dataPoints.length - 1].time;
    // get the first timestamp
    let firstTimestamp = dataPoints[0].time;
    // calculate the time difference in hours
    let timeDifference = (lastTimestamp - firstTimestamp) / 3600;
    // calculate the impulses per hour
    let impulsesPerHour = (dataPoints.length / timeDifference) / 3600;

    // reduce to 2 decimal
    console.log(impulsesPerHour.toFixed(2));

    return Math.round(impulsesPerHour * 100) / 100;

}


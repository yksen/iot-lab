#!/bin/bash

DEVICE_ID="kcQvFok0S4rahYTnfezRTIx3"
DEVICE_TOKEN="maker:4jrExtJr32uoVBrPrnJN7K23URhTkHStLJ8LFUMO"

simulate_measurement () {
    local measurement=$1
    local value=$2

    curl -X PUT "http://api.allthingstalk.io/device/${DEVICE_ID}/asset/${measurement}/state"    \
         -H "Authorization: Bearer ${DEVICE_TOKEN}"                                             \
         -H "Content-Type: application/json"                                                    \
         -d "{ \"value\": ${value} }"
}

for i in {1..100}; do
    temperature=$(((RANDOM + RANDOM) % 30))
    humidity=$(((RANDOM + RANDOM) % 100))

    echo "[Event no. ${i}]: temperature = ${temperature}, humidity = ${humidity}"

    simulate_measurement "temperature" ${temperature}
    simulate_measurement "humidity" ${humidity}

    sleep 5
done

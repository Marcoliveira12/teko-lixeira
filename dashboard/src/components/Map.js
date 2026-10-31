import { useState } from 'react';
import { MapContainer, TileLayer, Marker } from 'react-leaflet';
import markerIconPng from "leaflet/dist/images/marker-icon.png"
import {Icon} from 'leaflet'

function MyMap() {
  const [position] = useState([-1.4452047203496743, -48.4784512001505]);
  return (
    <MapContainer center={position} zoom={20} style={{ height: "300px", width: "750px", borderRadius: "10px" }}>
      <TileLayer
        url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
        attribution='© <a href="http://osm.org/copyright">OpenStreetMap</a> contributors'
      />
      <Marker position={position} icon={new Icon({iconUrl: markerIconPng, iconSize: [25, 41], iconAnchor: [12, 41]})}>
      </Marker>
    </MapContainer>
  );
}

export default MyMap;
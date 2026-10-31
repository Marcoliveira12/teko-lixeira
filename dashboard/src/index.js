import React from 'react';
import ReactDOM from 'react-dom/client';
import './index.css';
import Dashboard from './screens/Dashboard/DashBoard';
import { UserProvider } from './contexts/authenticatedContext';
//import Login from './screens/LoginPage';
import { BrowserRouter as Router, Route, Routes } from 'react-router-dom';


const root = ReactDOM.createRoot(document.getElementById('root'));
root.render(
  <React.StrictMode>
    <UserProvider>
      <Router>
        <Routes> {/* Envolve todas as rotas */}
          <Route path="/Dashboard" element={<Dashboard />} /> 
          <Route path="/" element={<Dashboard/>} />
        </Routes>
      </Router>
    </UserProvider>
  </React.StrictMode>
);



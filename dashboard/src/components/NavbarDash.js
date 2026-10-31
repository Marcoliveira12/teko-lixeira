import React, { useContext } from 'react';
import { UserContext } from '../contexts/authenticatedContext';
import tekolixeira from '../img/tekomente.png';

function Navbar({ theme, onToggleTheme }) {
  const { currentUser } = useContext(UserContext);

  const navbarStyle = {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    background: 'linear-gradient(to right, #06452A, #0A8754, #06452A)', // Escuro-Claro-Escuro
    padding: '8px',
    color: '#D1D5DB',
    height: '60px',
    transition: 'background 0.5s ease-in-out', // Transição suave
    marginBottom: '25px'
  };
  const tituloStyle = {
    fontSize: '28px',
    fontWeight: '700',
    fontFamily: "'Segoe UI', Tahoma, Geneva, Verdana, sans-serif",
    margin: 0,
    flexGrow: 1,
  };
  return (
    <div style={navbarStyle}>
      <div style={{ width: '90px' }}>
      </div>
      <div>
        <h2 style={tituloStyle}>Meu Dashboard</h2>
      </div>
      <div style={{ display: 'flex', alignItems: 'center' }}>
        {onToggleTheme && (
          <button
            className='themeToggle'
            onClick={onToggleTheme}
            title={theme === 'dark' ? 'Mudar para modo claro' : 'Mudar para modo escuro'}
            aria-label={theme === 'dark' ? 'Mudar para modo claro' : 'Mudar para modo escuro'}
          >
            {theme === 'dark' ? '☀️' : '🌙'}
          </button>
        )}
        <span>{currentUser ? currentUser.name : ""}</span>
        <img
          src={tekolixeira}
          alt="Descrição da Imagem"
          style={{ borderRadius: '50px', height: '50px', width: '50px' }}
        />
      </div>
    </div>
  );
}

export default Navbar;
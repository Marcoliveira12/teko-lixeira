import React, { createContext, useState } from 'react';

// Criação do Context
export const UserContext = createContext();

// Componente Provider
export const UserProvider = ({ children }) => {
    const [user, setUser] = useState({});

    return (
        <UserContext.Provider value={{ user, setUser }}>
            {children}
        </UserContext.Provider>
    );
};
O nosso Projeto apresenta alguns problemas nos comandos SUBSCRIBE e UNSUBSCRIBE, porque o resultado devolvido pelo servidor só aparece no STDOUT do cliente após fechar o servidor. 
Há outro problema que ficou por resolver que foi no envio dos pares chave-valor do servidor, porque enviava mesmo não tendo sido executado o comando SUBSCRIBE no STDIN do cliente.
Pensamos que o problema esteja no parsing da chave na função thread_manage_fifo.
Foi deixado no api.c um esboço de código que seria a nossa introdução ao exercício 2
